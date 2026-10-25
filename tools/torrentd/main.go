// torrentd: the P2P engine the TV app starts when a source is only a torrent (TorBox has
// no cached copy). It answers like Stremio's streaming server on 127.0.0.1, so the app
// plays http://127.0.0.1:11490/{infoHash}/{fileIdx} as any other link. Pieces live in a
// RAM buffer with a hard cap (mem.go), uploads are capped low, and it exits on its own
// when nothing has played for a while or the app is gone.
package main

import (
	"encoding/hex"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"log"
	"log/slog"
	"mime"
	"net"
	"net/http"
	"os"
	"path"
	"path/filepath"
	"runtime/debug"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"github.com/anacrolix/torrent"
	"github.com/anacrolix/torrent/bencode"
	"github.com/anacrolix/torrent/metainfo"
	"github.com/anacrolix/torrent/storage"
	"golang.org/x/time/rate"
)

var defaultTrackers = []string{
	"udp://tracker.opentrackr.org:1337/announce",
	"udp://open.stealth.si:80/announce",
	"udp://tracker.torrent.eu.org:451/announce",
	"udp://exodus.desync.com:6969/announce",
	"udp://open.demonii.com:1337/announce",
}

type server struct {
	cl      *torrent.Client
	store   *memStorage
	cacheB  int64
	active  atomic.Int64 // streams being served
	lastUse atomic.Int64 // unix seconds
	mu      sync.Mutex
	rates   map[metainfo.Hash]*rateSample
}

type rateSample struct {
	last  int64
	bytes int64
	bps   int64
}

func main() {
	addr := flag.String("addr", "127.0.0.1:11490", "where the app asks")
	cacheMB := flag.Int("cache", 160, "RAM for pieces, MB")
	idle := flag.Duration("idle", 5*time.Minute, "exit after this long with nothing playing")
	ppid := flag.Int("ppid", 0, "exit when this process is gone")
	upKB := flag.Int("up", 256, "upload cap, KB/s")
	seed := flag.String("seed", "", "tests: seed this file and print its info hash")
	seedPort := flag.Int("seed-port", 6990, "tests: the seeder's port")
	flag.Parse()
	log.SetFlags(log.Ltime)
	if *seed != "" {
		runSeeder(*seed, *seedPort)
		return
	}
	cache := int64(*cacheMB) << 20
	debug.SetMemoryLimit(cache + 96<<20) // the Go heap stays near the buffer
	s := &server{store: newMemStorage(cache), cacheB: cache, rates: map[metainfo.Hash]*rateSample{}}
	cfg := torrent.NewDefaultClientConfig()
	cfg.DefaultStorage = s.store
	cfg.Seed = false
	cfg.ListenPort = 0
	cfg.DataDir = os.TempDir()
	cfg.UploadRateLimiter = rate.NewLimiter(rate.Limit(*upKB<<10), *upKB<<10)
	cfg.DisableIPv6 = !haveIPv6()
	cfg.Slogger = slog.New(slog.NewTextHandler(os.Stderr, &slog.HandlerOptions{Level: slog.LevelError}))
	cl, err := torrent.NewClient(cfg)
	if err != nil {
		log.Fatalf("torrentd: %v", err)
	}
	s.cl = cl
	s.lastUse.Store(time.Now().Unix())
	go s.sampleRates()
	go s.exitWhenIdle(*idle, *ppid)
	ln, err := net.Listen("tcp", *addr)
	if err != nil {
		log.Fatalf("torrentd: %v", err)
	}
	log.Printf("torrentd: on %s, %d MB of RAM for pieces, uploads up to %d KB/s", *addr, *cacheMB, *upKB)
	log.Fatal(http.Serve(ln, s))
}

// Some TVs (and containers) have no IPv6 at all; the client would refuse to start.
func haveIPv6() bool {
	ln, err := net.Listen("tcp6", "[::1]:0")
	if err != nil {
		return false
	}
	ln.Close()
	return true
}

func (s *server) exitWhenIdle(idle time.Duration, ppid int) {
	for range time.Tick(2 * time.Second) {
		if ppid > 0 && os.Getppid() != ppid {
			log.Printf("torrentd: the app is gone, stopping")
			os.Exit(0)
		}
		if s.active.Load() == 0 && time.Since(time.Unix(s.lastUse.Load(), 0)) > idle {
			log.Printf("torrentd: idle for %v, stopping", idle)
			os.Exit(0)
		}
	}
}

func (s *server) sampleRates() {
	for range time.Tick(time.Second) {
		now := time.Now().UnixMilli()
		s.mu.Lock()
		for _, t := range s.cl.Torrents() {
			st := t.Stats()
			got := st.BytesReadUsefulData.Int64()
			r := s.rates[t.InfoHash()]
			if r == nil {
				r = &rateSample{last: now, bytes: got}
				s.rates[t.InfoHash()] = r
			}
			if dt := now - r.last; dt > 0 {
				r.bps = (got - r.bytes) * 1000 / dt
			}
			r.last, r.bytes = now, got
		}
		s.mu.Unlock()
	}
}

func (s *server) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	s.lastUse.Store(time.Now().Unix())
	parts := strings.Split(strings.Trim(r.URL.Path, "/"), "/")
	switch {
	case r.URL.Path == "/health":
		io.WriteString(w, "ok")
	case r.URL.Path == "/stats.json":
		s.stats(w)
	case r.URL.Path == "/cache": // the app's memory watch resizes the piece cache as memory goes
		if mb, err := strconv.Atoi(r.URL.Query().Get("mb")); err == nil && mb >= 32 {
			s.cacheB = int64(mb) << 20
			s.store.setCap(s.cacheB)
			debug.SetMemoryLimit(s.cacheB + 96<<20)
			log.Printf("torrentd: %d MB of RAM for pieces now", mb)
		}
		io.WriteString(w, "ok")
	case len(parts) == 2 && parts[1] == "drop":
		if ih, ok := parseHash(parts[0]); ok {
			if t, found := s.cl.Torrent(ih); found {
				t.Drop()
				log.Printf("torrentd: dropped %s", ih.HexString())
			}
		}
		io.WriteString(w, "ok")
	case len(parts) == 2:
		s.stream(w, r, parts[0], parts[1])
	default:
		http.NotFound(w, r)
	}
}

func parseHash(s string) (metainfo.Hash, bool) {
	var ih metainfo.Hash
	b, err := hex.DecodeString(s)
	if err != nil || len(b) != 20 {
		return ih, false
	}
	copy(ih[:], b)
	return ih, true
}

func (s *server) torrentFor(ih metainfo.Hash, trackers []string) (*torrent.Torrent, error) {
	if t, ok := s.cl.Torrent(ih); ok {
		return t, nil
	}
	tiers := [][]string{}
	for _, tr := range append(trackers, defaultTrackers...) {
		tiers = append(tiers, []string{tr})
	}
	t, _, err := s.cl.AddTorrentSpec(&torrent.TorrentSpec{AddTorrentOpts: torrent.AddTorrentOpts{InfoHash: ih}, Trackers: tiers})
	if err != nil {
		return nil, err
	}
	s.store.attach(ih, t)
	if peers := os.Getenv("SNTV_TORRENT_PEERS"); peers != "" { // tests: a local seeder
		var pi []torrent.PeerInfo
		for _, a := range strings.Split(peers, ",") {
			if ta, err := net.ResolveTCPAddr("tcp", a); err == nil {
				pi = append(pi, torrent.PeerInfo{Addr: ta, Trusted: true})
			}
		}
		t.AddPeers(pi)
	}
	log.Printf("torrentd: added %s", ih.HexString())
	return t, nil
}

// posReader keeps the storage told where this player is reading.
type posReader struct {
	torrent.Reader
	s    *memStorage
	head *playHead
	base int64
	pos  int64
}

func (p *posReader) Read(b []byte) (int, error) {
	n, err := p.Reader.Read(b)
	p.pos += int64(n)
	p.s.moveHead(p.head, p.base+p.pos)
	return n, err
}

func (p *posReader) Seek(off int64, whence int) (int64, error) {
	pos, err := p.Reader.Seek(off, whence)
	if err == nil {
		p.pos = pos
		p.s.moveHead(p.head, p.base+pos)
	}
	return pos, err
}

func (s *server) stream(w http.ResponseWriter, r *http.Request, hash, idx string) {
	ih, ok := parseHash(hash)
	if !ok {
		http.Error(w, "bad info hash", http.StatusBadRequest)
		return
	}
	t, err := s.torrentFor(ih, r.URL.Query()["tr"])
	if err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
	s.active.Add(1)
	defer func() { s.active.Add(-1); s.lastUse.Store(time.Now().Unix()) }()
	select {
	case <-t.GotInfo():
	case <-time.After(90 * time.Second):
		http.Error(w, "no peers sent the torrent's file list in 90 s", http.StatusGatewayTimeout)
		return
	case <-r.Context().Done():
		return
	}
	files := t.Files()
	var f *torrent.File
	if i, err := strconv.Atoi(idx); err == nil && i >= 0 && i < len(files) {
		f = files[i]
	} else { // -1: the largest file, as the streaming server does
		for _, c := range files {
			if f == nil || c.Length() > f.Length() {
				f = c
			}
		}
	}
	if f == nil {
		http.Error(w, "the torrent has no files", http.StatusNotFound)
		return
	}
	ahead := s.cacheB / 4
	if ahead > 64<<20 {
		ahead = 64 << 20
	}
	rd := f.NewReader()
	defer rd.Close()
	rd.SetContext(r.Context())
	rd.SetResponsive()
	rd.SetReadahead(ahead)
	head := &playHead{pos: f.Offset(), ahead: ahead}
	s.store.addHead(ih, head)
	defer s.store.removeHead(ih, head)
	if ct := mime.TypeByExtension(path.Ext(f.DisplayPath())); ct != "" {
		w.Header().Set("Content-Type", ct)
	} else if strings.HasSuffix(strings.ToLower(f.DisplayPath()), ".mkv") {
		w.Header().Set("Content-Type", "video/x-matroska")
	}
	http.ServeContent(w, r, f.DisplayPath(), time.Time{}, &posReader{Reader: rd, s: s.store, head: head, base: f.Offset()})
}

func (s *server) stats(w http.ResponseWriter) {
	type item struct {
		Hash     string `json:"hash"`
		Name     string `json:"name"`
		Peers    int    `json:"peers"`
		Seeders  int    `json:"seeders"`
		DownBps  int64  `json:"downBps"`
		UpBytes  int64  `json:"upBytes"`
		HaveInfo bool   `json:"haveInfo"`
	}
	out := struct {
		Torrents []item `json:"torrents"`
		MemBytes int64  `json:"memBytes"`
		CapBytes int64  `json:"capBytes"`
	}{MemBytes: s.store.usedBytes(), CapBytes: s.cacheB}
	s.mu.Lock()
	for _, t := range s.cl.Torrents() {
		st := t.Stats()
		it := item{Hash: t.InfoHash().HexString(), Peers: st.ActivePeers, Seeders: st.ConnectedSeeders, UpBytes: st.BytesWrittenData.Int64()}
		if t.Info() != nil {
			it.Name, it.HaveInfo = t.Name(), true
		}
		if r := s.rates[t.InfoHash()]; r != nil {
			it.DownBps = r.bps
		}
		out.Torrents = append(out.Torrents, it)
	}
	s.mu.Unlock()
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(out)
}

// runSeeder serves one file to a local peer, for the app's tests.
func runSeeder(file string, port int) {
	info := metainfo.Info{PieceLength: 256 << 10}
	if err := info.BuildFromFilePath(file); err != nil {
		log.Fatalf("torrentd: %v", err)
	}
	mi := metainfo.MetaInfo{}
	var err error
	if mi.InfoBytes, err = bencode.Marshal(info); err != nil {
		log.Fatalf("torrentd: %v", err)
	}
	cfg := torrent.NewDefaultClientConfig()
	cfg.Seed = true
	cfg.NoDHT = true
	cfg.DisableIPv6 = !haveIPv6()
	cfg.ListenPort = port
	cfg.DataDir = os.TempDir()
	cfg.DefaultStorage = storage.NewFileWithCompletion(filepath.Dir(file), storage.NewMapPieceCompletion())
	cfg.Slogger = slog.New(slog.NewTextHandler(os.Stderr, &slog.HandlerOptions{Level: slog.LevelError}))
	cl, err := torrent.NewClient(cfg)
	if err != nil {
		log.Fatalf("torrentd: %v", err)
	}
	t, err := cl.AddTorrent(&mi)
	if err != nil {
		log.Fatalf("torrentd: %v", err)
	}
	t.VerifyData()
	fmt.Println(mi.HashInfoBytes().HexString())
	os.Stdout.Sync()
	select {}
}
