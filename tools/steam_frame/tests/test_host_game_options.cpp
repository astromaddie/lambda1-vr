#include "HostGameOptions.h"

#include <cassert>

int main()
{
	assert( HostGame_NormalizeMaxPlayers( -1 ) == 2 );
	assert( HostGame_NormalizeMaxPlayers( 0 ) == 2 );
	assert( HostGame_NormalizeMaxPlayers( 1 ) == 2 );
	assert( HostGame_NormalizeMaxPlayers( 2 ) == 2 );
	assert( HostGame_NormalizeMaxPlayers( 12 ) == 12 );
	assert( HostGame_NormalizeMaxPlayers( 32 ) == 32 );
	assert( HostGame_NormalizeMaxPlayers( 33 ) == 32 );
	return 0;
}
