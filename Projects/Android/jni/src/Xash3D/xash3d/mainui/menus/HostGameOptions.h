#ifndef XASH_HOST_GAME_OPTIONS_H
#define XASH_HOST_GAME_OPTIONS_H

// A listen server needs a slot for its local player and at least one remote player.
static inline int HostGame_NormalizeMaxPlayers( int players )
{
	if( players < 2 ) return 2;
	if( players > 32 ) return 32;
	return players;
}

#endif // XASH_HOST_GAME_OPTIONS_H
