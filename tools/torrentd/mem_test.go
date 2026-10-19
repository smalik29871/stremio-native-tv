package main

import (
	"testing"

	"github.com/anacrolix/torrent/metainfo"
)

// Pieces past the cap go, oldest first, but never the ones around a play head.
func TestMemStorageStaysUnderCap(t *testing.T) {
	const piece = 256 << 10
	s := &memStorage{cap: 4 * piece, ts: map[metainfo.Hash]*memTorrent{}}
	var ih metainfo.Hash
	mt := &memTorrent{s: s, ih: ih, pieces: map[int]*memPiece{}, heads: map[*playHead]bool{}}
	s.ts[ih] = mt
	head := &playHead{pos: 9 * piece, ahead: piece} // reading piece 9, piece 10 next
	mt.heads[head] = true
	data := make([]byte, piece)
	for i := 0; i < 12; i++ {
		mp := &memPiece{mt: mt, index: i, length: piece, offset: int64(i) * piece}
		mt.pieces[i] = mp
		mp.WriteAt(data, 0)
		mp.MarkComplete()
	}
	s.evict()
	if s.used > s.cap {
		t.Fatalf("%d bytes held, cap %d", s.used, s.cap)
	}
	for _, i := range []int{9, 10} {
		if !mt.pieces[i].Completion().Complete {
			t.Errorf("piece %d near the play head was dropped", i)
		}
	}
	if mt.pieces[0].Completion().Complete {
		t.Errorf("the oldest piece far from the head was kept")
	}
	buf := make([]byte, 16)
	if _, err := mt.pieces[0].ReadAt(buf, 0); err == nil {
		t.Errorf("a dropped piece still reads")
	}
}
