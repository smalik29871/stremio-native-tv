package main

// Piece storage in RAM with a hard cap, so streaming never writes the TV's flash. Pieces
// near what is being played stay; when the cap is reached the least recently used
// complete piece outside those windows is dropped, and the client is told it is missing
// (it downloads it again if the viewer seeks back to it).

import (
	"context"
	"errors"
	"sync"
	"time"

	"github.com/anacrolix/torrent"
	"github.com/anacrolix/torrent/metainfo"
	"github.com/anacrolix/torrent/storage"
)

type memStorage struct {
	cap  int64
	mu   sync.Mutex
	used int64
	ts   map[metainfo.Hash]*memTorrent
}

type memTorrent struct {
	s      *memStorage
	ih     metainfo.Hash
	pieces map[int]*memPiece
	t      *torrent.Torrent   // set once added, for telling it about dropped pieces
	heads  map[*playHead]bool // where the players are reading (byte offsets in the torrent)
}

type memPiece struct {
	mt       *memTorrent
	index    int
	length   int64
	offset   int64 // in the torrent
	buf      []byte
	complete bool
	used     time.Time
}

type playHead struct{ pos, ahead int64 }

func newMemStorage(capBytes int64) *memStorage {
	s := &memStorage{cap: capBytes, ts: map[metainfo.Hash]*memTorrent{}}
	go s.evictLoop()
	return s
}

func (s *memStorage) OpenTorrent(_ context.Context, info *metainfo.Info, ih metainfo.Hash) (storage.TorrentImpl, error) {
	s.mu.Lock()
	mt := s.ts[ih]
	if mt == nil {
		mt = &memTorrent{s: s, ih: ih, pieces: map[int]*memPiece{}, heads: map[*playHead]bool{}}
		s.ts[ih] = mt
	}
	s.mu.Unlock()
	return storage.TorrentImpl{
		Piece: func(p metainfo.Piece) storage.PieceImpl {
			s.mu.Lock()
			defer s.mu.Unlock()
			mp := mt.pieces[p.Index()]
			if mp == nil {
				mp = &memPiece{mt: mt, index: p.Index(), length: p.Length(), offset: p.Offset()}
				mt.pieces[p.Index()] = mp
			}
			return mp
		},
		Close: func() error {
			s.mu.Lock()
			defer s.mu.Unlock()
			for _, mp := range mt.pieces {
				s.used -= int64(len(mp.buf))
			}
			delete(s.ts, ih)
			return nil
		},
	}, nil
}

func (s *memStorage) Close() error { return nil }

func (s *memStorage) attach(ih metainfo.Hash, t *torrent.Torrent) {
	s.mu.Lock()
	if mt := s.ts[ih]; mt != nil {
		mt.t = t
	}
	s.mu.Unlock()
}

func (s *memStorage) addHead(ih metainfo.Hash, h *playHead) {
	s.mu.Lock()
	if mt := s.ts[ih]; mt != nil {
		mt.heads[h] = true
	}
	s.mu.Unlock()
}

func (s *memStorage) removeHead(ih metainfo.Hash, h *playHead) {
	s.mu.Lock()
	if mt := s.ts[ih]; mt != nil {
		delete(mt.heads, h)
	}
	s.mu.Unlock()
}

func (s *memStorage) moveHead(h *playHead, pos int64) {
	s.mu.Lock()
	h.pos = pos
	s.mu.Unlock()
}

func (s *memStorage) usedBytes() int64 {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.used
}

// near: inside a window someone is reading or about to read. A short rewind stays
// instant: up to 8 MB behind the head are kept (an eighth of the buffer when smaller).
func (mt *memTorrent) near(mp *memPiece) bool {
	keepBehind := mt.s.cap / 8
	if keepBehind > 8<<20 {
		keepBehind = 8 << 20
	}
	for h := range mt.heads {
		if mp.offset+mp.length > h.pos-keepBehind && mp.offset < h.pos+h.ahead {
			return true
		}
	}
	return false
}

func (s *memStorage) evictLoop() {
	for range time.Tick(300 * time.Millisecond) {
		s.evict()
	}
}

func (s *memStorage) evict() {
	type drop struct {
		t     *torrent.Torrent
		index int
	}
	var dropped []drop
	s.mu.Lock()
	for s.used > s.cap {
		var victim *memPiece
		for _, mt := range s.ts {
			for _, mp := range mt.pieces {
				if mp.buf == nil || !mp.complete || mt.near(mp) {
					continue
				}
				if victim == nil || mp.used.Before(victim.used) {
					victim = mp
				}
			}
		}
		if victim == nil {
			break // everything held is in use: the cap is soft until the heads move on
		}
		s.used -= int64(len(victim.buf))
		victim.buf = nil
		victim.complete = false
		if victim.mt.t != nil {
			dropped = append(dropped, drop{victim.mt.t, victim.index})
		}
	}
	s.mu.Unlock()
	for _, d := range dropped { // outside our lock: this takes the client's
		d.t.Piece(d.index).UpdateCompletion()
	}
}

var errNoData = errors.New("piece not in memory")

func (mp *memPiece) ReadAt(p []byte, off int64) (int, error) {
	s := mp.mt.s
	s.mu.Lock()
	defer s.mu.Unlock()
	if mp.buf == nil || off >= int64(len(mp.buf)) {
		return 0, errNoData
	}
	mp.used = time.Now()
	n := copy(p, mp.buf[off:])
	if n < len(p) {
		return n, errNoData
	}
	return n, nil
}

func (mp *memPiece) WriteAt(p []byte, off int64) (int, error) {
	s := mp.mt.s
	s.mu.Lock()
	defer s.mu.Unlock()
	if mp.buf == nil {
		mp.buf = make([]byte, mp.length)
		s.used += mp.length
	}
	mp.used = time.Now()
	return copy(mp.buf[off:], p), nil
}

func (mp *memPiece) MarkComplete() error {
	s := mp.mt.s
	s.mu.Lock()
	mp.complete = mp.buf != nil
	s.mu.Unlock()
	return nil
}

func (mp *memPiece) MarkNotComplete() error {
	s := mp.mt.s
	s.mu.Lock()
	mp.complete = false
	s.mu.Unlock()
	return nil
}

func (mp *memPiece) Completion() storage.Completion {
	s := mp.mt.s
	s.mu.Lock()
	defer s.mu.Unlock()
	return storage.Completion{Ok: true, Complete: mp.complete && mp.buf != nil}
}
