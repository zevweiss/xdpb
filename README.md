# xdpb
--------

X Display Pointer Barriers

This is a simple program to set up X11 pointer barriers at the edges
of each display.  This can make it much easier to position the mouse
pointer on UI elements (such as scroll bars) that often end up at or
near "internal" screen edges (edges that border other screens) in
multi-head setups.

### Usage

    xdpb [ -h | -d DISTANCE | -s SPEED | -m SECONDS ]

`xdpb`'s only options (aside from `-h` for a usage message) select the
mechanism for releasing the pointer from a barrier.  `xdpb` offers
three modes of operation for this: distance, speed, and double-tap.

In distance mode (selected by the `-d DISTANCE` flag), the pointer is
released when it reaches a threshold of pixels (the `DISTANCE`
parameter) it would have traveled beyond the barrier were the barrier
not constraining it.  Sensible values for `DISTANCE` might be in the
range of a few hundred.

In speed mode (select by the `-s SPEED` flag), the pointer is released
when its speed exceeds a given threshold (the `SPEED` parameter).  If
the pointer's speed exceeds this threshold when it first hits the
barrier it will not be stopped at all.  Like with distance mode, this
is the speed at which the pointer would move were the barrier not
constraining it.  The units of `SPEED` aren't terribly intuitive
(pixels reported in a given `XI_BarrierHit` event), but sensible
values for it might be in the range of a few dozen or so.

In double-tap mode (selected by the `-m SECONDS` flag), the pointer is
released when it is "tapped" against the barrier twice (i.e. moved
against the barrier, away from it, and then back against it) within a
given amount of time (the `SECONDS` parameter).  Sensible values for
`SECONDS` might be in the range of 0.25-1.5 or so.

### License

`xdpb` is released under the terms of the ISC License (see
`LICENSE`).
