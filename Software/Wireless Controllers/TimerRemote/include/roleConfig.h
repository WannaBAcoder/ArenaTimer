#ifndef ROLE_CONFIG_H
#define ROLE_CONFIG_H

// ReadyRemote only: this batch has no hardware Red/Blue strap (a future
// revision should add one), so role is set here by hand before flashing
// each physical unit. Change this, reflash, move to the next board.
//
//   1 = Red
//   2 = Blue

#define READY_REMOTE_COLOR 1

#endif
