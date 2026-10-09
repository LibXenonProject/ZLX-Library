// lzx.cpp : Defines the entry point for the application.
//


#include "Console.h"
#include "Draw.h"
#include "Bgshader.h"
#ifdef LIBXENON

#include <debug.h>
#include <xenos/xenos.h>

#include <diskio/ata.h>
extern "C" {
#include <input/input.h>
#include <xenon_nand/xenon_sfcx.h>
}
#include <console/console.h>
#include <diskio/diskio.h>
#include <usb/usbmain.h>
#include <time/time.h>
#include <xenon_soc/xenon_power.h>
#include <xenon_smc/xenon_smc.h>

#include <dirent.h>
#include <elf/elf.h>
#else
#include "posix\dirent.h"
#include "posix\input.h"
#endif
#include <stdio.h>
#include <algorithm>
#include <vector>
#include <string>
#include <math.h>

#include "menu.h"
#include "mount.h"

using namespace ZLX;

MyConsole App;

#define MAX_ACTION 16
int nbAction = 0;
ActionEntry * ActionEntries[MAX_ACTION];
static char basepath[256] = {0};

void append_dir_to_path(char * path, char * dir) {
    if (!strcmp(dir, "..")) {
        int i = strlen(path);
        int delimcount = 0;

        while (i >= 0 && delimcount < 2) {
            if (path[i] == '/') {
                ++delimcount;

                if (delimcount > 1) {
                    path[i + 1] = '\0';
                }
            }
            --i;
        }
    } else if (!strcmp(dir, ".")) {
        return;
    } else {
        strcat(path, dir);
        strcat(path, "/");
    }
}

bool vEntrySort(FileEntry i, FileEntry j) {
    if (i.type == j.type) {
        return i.name < j.name;
    } else {
        return (i.type > j.type);
    }
}

void MyConsole::RenderProgress() {

    float posX = -0.5f;
    float posY = -0.15f;

    float w = 1.f;
    float h = 0.3f;

    float borderSize = 0.03f;

    // white border
    Draw::DrawColoredRect(posX, posY, w, h, 0xFFFFFFFF);
    // black bg
    Draw::DrawColoredRect(
            posX + (borderSize / 2),
            posY + (borderSize / 2),
            w - borderSize,
            h - borderSize,
            0xFF000000
            );

    int PctColor = 0xFF00FF00;

    // progress things ...
    Draw::DrawColoredRect(
            posX + (borderSize / 2),
            posY + (borderSize / 2),
            (w - borderSize) * progressPct,
            h - borderSize,
            PctColor
            );
}

void MyConsole::RenderFileInfoPanel() {
    if (vEntry.size() == 0)
        return;

    unsigned int color = DefaultColor;
    float leftOffset = -1.f;
    float topOffset = 0.65f;

    if (panelSelected == PANEL_FILE_LIST) {
        color &= FocusColor;
    } else {
        color &= BlurColor;
    }

    char bottomInfo[256];
    sprintf(bottomInfo, "File %d/%d", (entrySelected + 1), vEntry.size());
    m_font.DrawTextF(bottomInfo, color, leftOffset, topOffset);
    m_font.DrawTextF("Some Info here ...", color, leftOffset, topOffset + 0.08f);
}

void MyConsole::RenderActionPanel() {
    float topOffset = -0.65f;
    float leftOffset = 0.2f;
    float lineHeight = 0.07f;

    for (int i = 0; i < nbAction; i++) {
        if (ActionEntries[i]) {
            float posY = (topOffset)+(lineHeight * (i)); // + topOffset;
            unsigned int color = DefaultColor;
            if (actionSelected == i) {
                color = SelectedColor;
            }

            if (panelSelected == PANEL_ACTION) {
                color &= FocusColor;
            } else {
                color &= BlurColor;
            }

            m_font.DrawTextF(ActionEntries[i]->name.c_str(), color, leftOffset, posY);
        }
    }
}

void MyConsole::RenderTopPanel() {
    // m_font.DrawTextF("Everyday i'm browsin'", 0xFFFFFFFF, -1.f, -1.f);		
    m_font.Scale(0.8f);
    m_font.DrawTextF(currentPath, 0xFFFFFFFF, -1.f, -0.83f);
    m_font.Scale(1.0f);
}

void MyConsole::RenderFileListPanel() {
    int start, end;
    float topOffset = -0.75f;
    float leftOffset = -1.f;
    float lineHeight = 0.07f;

    int size = vEntry.size();

    if (size == 0)
        return;

    start = entrySelected - MAX_DISPLAYED_ENTRIES / 2;
    if (start < 0)
        start = 0;

    end = MIN(MAX_DISPLAYED_ENTRIES, vEntry.size() - start) + start;

    if (end > size)
        end = size;

    for (int k = start; k < end; k++) {

        float posY = (topOffset)+(lineHeight * (k - start)); // + topOffset;
        unsigned int color = DefaultColor;
        if (entrySelected == k) {
            color = SelectedColor;
        }

        if (panelSelected == PANEL_FILE_LIST) {
            color &= FocusColor;
        } else {
            color &= BlurColor;
        }

        //printf("%s - %d\r\n", vEntry.at(k).name.c_str(), vEntry.at(k).type);


        if (vEntry.at(k).type == 1) {
            m_font.DrawTextF("[Dir]", color, -0.2f, posY);
        }
        m_font.DrawTextF(vEntry.at(k).name.c_str(), color, leftOffset, posY);
    }

}

void MyConsole::RenderApp() {
    // Begin to draw
    Begin();

    // Draw bg
    RenderBg();

    // Draw some text using font
    m_font.Begin();
    // need to fix that ..
    //m_font.DrawTextF("Error ?", SelectedColor, -0.55f, 0.3f);

    // Top part
    RenderTopPanel();

    // Render file listing
    RenderFileListPanel();

    // Render bottom part
    RenderFileInfoPanel();

    RenderActionPanel();

    if (panelSelected == PANEL_PROGRESS)
        RenderProgress();

    // Finished to draw font things ...
    m_font.End();

    // Draw all text + form
    Render();
    // Draw is finished
    End();
}

BOOL MyConsole::Warning(const char *message) {
    display_warning = 1;
    unsigned int color = 0xFF6f641a;

    memset(&old_ctrl, 0, sizeof (struct controller_data_s));
    memset(&ctrl, 0, sizeof (struct controller_data_s));

    while (display_warning == 1) {

        // Begin to draw
        Begin();

        // Draw bg
        RenderBg();

        Draw::DrawColoredRect(-0.5f, -0.5f, 1.0f, 1.0f, color);

        m_font.Begin();

        // need to fix that ..
        //m_font.DrawTextF("Error ?", SelectedColor, -0.55f, 0.3f);


        m_font.DrawTextF(message, SelectedColor, -0.55f, -0.5f);

        m_font.DrawTextF("Press X to continue", SelectedColor, -0.55f, 0.3f);
        m_font.DrawTextF("Press B to cancel", SelectedColor, 0.00f, 0.3f);

        m_font.End();

        // Draw all text + form
        Render();

        // Draw is finished
        End();

        // Update code ...
        {
            usb_do_poll();
            get_controller_data(&ctrl, 0);

            if (ctrl.b & !old_ctrl.b) {
                display_warning = 0;
                return FALSE;
            }
            if (ctrl.x & !old_ctrl.x) {
                display_warning = 0;
                return TRUE;
            }
            // Save the old value
            old_ctrl = ctrl;
            memset(&ctrl, 0, sizeof (struct controller_data_s));
        }
    }
}

void MyConsole::RenderBg() {
    if (bg == NULL)
        Bgshader::RenderBg();
    else
        Draw::DrawTexturedRect(-1.0f, -1.0f, 2.0f, 2.0f, bg);
}

void MyConsole::Alert(const char *message) {
    display_alert = 1;
    unsigned int color = 0xFF1212b4;

    memset(&old_ctrl, 0, sizeof (struct controller_data_s));
    memset(&ctrl, 0, sizeof (struct controller_data_s));

    while (display_alert == 1) {
        // Begin to draw
        Begin();

        RenderBg();

        Draw::DrawColoredRect(-0.5f, -0.5f, 1.0f, 1.0f, color);

        m_font.Begin();

        // need to fix that ..
        //m_font.DrawTextF("Error ?", SelectedColor, -0.55f, 0.3f);

        m_font.DrawTextF(message, SelectedColor, -0.55f, -0.5f);
        m_font.DrawTextF("Press B to continue", SelectedColor, -0.3f, 0.3f);

        m_font.End();

        // Draw all text + form
        Render();

        // Draw is finished
        End();

        // Update code ...
        {
            usb_do_poll();
            get_controller_data(&ctrl, 0);

            if (ctrl.b & !old_ctrl.b) {
                display_alert = 0;
                return;
            }
            // Save the old value
            old_ctrl = ctrl;
            memset(&ctrl, 0, sizeof (struct controller_data_s));
        }
    }
}

void MyConsole::SetProgressValue(float pct) {
    if (pct > 0.0f) {
        panelSelected = PANEL_PROGRESS;
        progressPct = pct;

        RenderApp();
    } else {
        panelSelected = PANEL_FILE_LIST;
    }
}

void MyConsole::Run() {
    Init();

    // Disable auto render on format
    SetAutoRender(0);

    //        strcpy(currentPath, "uda:/");
    handle = -1;
    const char * s = NULL;
    handle = bdev_enum(handle, &s);
    if (handle < 0) {
        strcpy(currentPath, "");
    } else {
        strcpy(currentPath, s);
        strcat(currentPath, ":/");
        ScanDir();
    }

    while (1) {
        RenderApp();
        Update();
    }
}

void MyConsole::ScanDir() {
    entrySelected = 0;

    DIR * rep = opendir(currentPath);

    vEntry.clear();

    if (rep != NULL) {
        struct dirent * ent;

        while ((ent = readdir(rep)) != NULL) {
            // never display hidden files
            if (ent->d_name[0] == '.')
                continue;

            FileEntry currentEntry;
            currentEntry.name = ent->d_name;
            currentEntry.type = (ent->d_type == DIR_TYPE) ? 1 : 0;
            vEntry.push_back(currentEntry);

        }

        closedir(rep);
    }
    // Sort array
    std::sort(vEntry.begin(), vEntry.end(), vEntrySort);

    return;
}

void MyConsole::Update() {
#ifdef WIN32
    W32Update();
#endif
    usb_do_poll();

    get_controller_data(&ctrl, 0);
    {
        //if (get_controller_data(&ctrl, 0)) {
        if (ctrl.up && !old_ctrl.up) {
            switch (panelSelected) {
                case PANEL_FILE_LIST:
                    entrySelected--;
                    break;
                case PANEL_ACTION:
                    actionSelected--;
                    break;
            }
        }

        if (ctrl.down && !old_ctrl.down) {
            switch (panelSelected) {
                case PANEL_FILE_LIST:
                    entrySelected++;
                    break;
                case PANEL_ACTION:
                    actionSelected++;
                    break;
            }
        }
        if (ctrl.b && !old_ctrl.b) {
            switch (panelSelected) {
                case PANEL_FILE_LIST:
                    append_dir_to_path(currentPath, "..");
                    ScanDir();
                    break;
            }
        }
        if (ctrl.select && !old_ctrl.select) {
            switch (panelSelected) {
                case PANEL_FILE_LIST:
                    const char * s;
                    do {
                        handle = bdev_enum(handle, &s);
                    } while (handle < 0);

                    strcpy(currentPath, s);
                    strcat(currentPath, ":/");
                    ScanDir();
                    break;
            }
        }
        if
            (
                (ctrl.start && !old_ctrl.start) || (ctrl.a && !old_ctrl.a)
                ) {
            switch (panelSelected) {
                case PANEL_FILE_LIST:
                {
                    if (vEntry.size() == 0)
                        break;

                    // entrySelected++;
                    FileEntry currentEntry = vEntry.at(entrySelected);

                    if (currentEntry.type == 1) {
                        append_dir_to_path(currentPath, (char*) currentEntry.name.c_str());
                        ScanDir();
                    } else {
                        sprintf(currentFile, "%s/%s", currentPath, currentEntry.name.c_str());
                        ActionLaunchFile(currentFile);
                    }
                    break;
                }
                case PANEL_ACTION:
                {
                    if (nbAction == 0)
                        break;

                    // Exec action ...
                    //FileEntry currentEntry = vEntry.at(entrySelected);
                    ActionEntry * currentAction = ActionEntries[actionSelected];
                    if (currentAction->action != NULL) {
                        //sprintf((char*)currentAction.param, "%s/%s", currentPath, currentEntry.name.c_str());
                        currentAction->param = (void*) currentPath;
                        currentAction->action(currentAction);
                    }
                    break;
                }
            }
        }

        if (ctrl.left && !old_ctrl.left) {
            //if(panelSelected!=PANEL_PROGRESS)
            panelSelected--;
        }
        if (ctrl.right && !old_ctrl.right) {
            //if(panelSelected!=PANEL_PROGRESS)
            panelSelected++;
        }

        // clamp ...
        CLAMP(entrySelected, 0, vEntry.size() - 1);
        CLAMP(actionSelected, 0, nbAction - 1);
        CLAMP(panelSelected, 0, 1);


        // Save the old value
        old_ctrl = ctrl;

        // Erase for the big loop
        memset(&ctrl, 0, sizeof (struct controller_data_s));
    }


}

void AddAction(ActionEntry * action) {
    if (action && nbAction < MAX_ACTION) {
        ActionEntries[nbAction] = action;
        nbAction++;
    }
}

void MyConsole::InitActionEntry() {
#if 1

    {
        // TFTP
        ActionEntry * action = new ActionEntry();
        action->name = "Boot TFTP";
        action->action = ActionBootTFTP;
        action->param = NULL;
        AddAction(action);
    }
    {
        // Shutdown
        ActionEntry * action = new ActionEntry();
        action->name = "Shutdown";
        action->action = ActionShutdown;
        action->param = NULL;
        AddAction(action);
    }
    {
        // Restart
        ActionEntry * action = new ActionEntry();
        action->name = "Restart";
        action->action = ActionRestart;
        action->param = NULL;
        AddAction(action);
    }

    {
        // Dump nand
        ActionEntry * action = new ActionEntry();
        action->name = "Dump nand";
        action->action = ActionDumpNand;
        action->param = NULL;
        AddAction(action);
    }
#endif    
}

void MyConsole::Init() {
    entrySelected = 0;
    panelSelected = 0;
    actionSelected = 0;

    display_warning = 0;
    display_alert = 0;

    progressPct = 0.0f;

    InitActionEntry();
	char *ressourcesPath = (char*)malloc(256);
	char *bgPath = (char*)malloc(256);
	sprintf(ressourcesPath,"%s/ressources/",basepath);
	sprintf(bgPath,"%s/ressources/bg.png",basepath);
    Create(ressourcesPath, 0xFF0000FF, 0xFFFFFF00);
    LoadTextureFromFile(g_pVideoDevice, bgPath, &bg);
};

int main(int argc, char **argv) {
	
	if(argc != 0 && argv[0]){
		char *tmp = argv_GetFilepath(argv[0]);
		strcpy(basepath,tmp);
	} else {
		strcpy(basepath,"uda0:");
	}
	
    usb_init();
    usb_do_poll();
    xenon_ata_init();
    xenon_atapi_init();
    
    mount_all_devices();
    findDevices();
    
    xenon_smc_start_bootanim();
    sfcx_init();

    App.Run();

    while (1) {
    }

    return 0;
}
