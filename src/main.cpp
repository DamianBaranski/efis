#include "data_manager_stratux.h"
#include "ahrs_widget.h"
#include "terrain_widget.h"
#include "screen.h"



int main()
{
	//Create screen object
	Screen screen(1024, 600);
	
	//Create data manager object
	DataManagerStratux dataManager;

	//Create terrain widget
	TerrainWidget terrainWidget(screen, dataManager);

	//Create ahrs widget
	AhrsWidget ahrsWidget(screen, dataManager);

	//Start data manager thread
	dataManager.start();

	//Start screen main loop
	screen.mainLoop();

}
