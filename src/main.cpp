#include "data_manager_stratux.h"
#include "attitude_widget.h"
#include "screen.h"

int main()
{
	//Create screen object
	Screen screen(800, 600);
	//Create data manager object
	DataManagerStratux dataManager;
	//Create attitude widget
	AttitudeWidget attitudeWidget(screen, dataManager);
	//Start data manager thread
	dataManager.start();
	//Start screen main loop
	screen.mainLoop();
}
