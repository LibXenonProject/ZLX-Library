#include <Browser.h>
#include <Hw.h>
#include <elf/elf.h>
#include "action.h"

using namespace ZLX;

Browser App;

char FUSES[256];
static char basepath[256] = {0};

int main(int argc, char **argv) {
	
	if(argc != 0 && argv[0]){
		char *tmp = argv_GetFilepath(argv[0]);
		sprintf(basepath,"%s/ressources/",tmp);
	} else {
		strcpy(basepath,"uda0:/ressources/");
	}
    
    Hw::SystemInit(INIT_SOUND|INIT_VIDEO|INIT_USB|INIT_ATA|INIT_ATAPI|INIT_FILESYSTEM|INIT_SFCX);
    {
        lpBrowserActionEntry action = new BrowserActionEntry();
        action->name = "Return to xell";
        action->action = ActionReturnToXell;
        action->param = NULL;
        App.AddAction(action);
    }
    {
        lpBrowserActionEntry action = new BrowserActionEntry();
        action->name = "Boot tftp";
        action->action = ActionBootTFTP;
        action->param = NULL;
        App.AddAction(action);
    }
    {
        lpBrowserActionEntry action = new BrowserActionEntry();
        action->name = "Dump nand";
        action->action = ActionDumpNand;
        action->param = NULL;
        App.AddAction(action);
    }
    {
        lpBrowserActionEntry action = new BrowserActionEntry();
        action->name = "Shutdown";
        action->action = ActionShutdown;
        action->param = NULL;
        App.AddAction(action);
    }
    {
        lpBrowserActionEntry action = new BrowserActionEntry();
        action->name = "Restart";
        action->action = ActionRestart;
        action->param = NULL;
        App.AddAction(action);
    }

    App.SetLaunchAction(ActionLaunchFile);
    App.Run(basepath);
}
