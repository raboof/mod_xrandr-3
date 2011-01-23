/*
 * Ion xrandr module
 * Copyright (C) 2004 Ragnar Rova
 *               2005-2007 Tuomo Valkonen
 *
 * by Ragnar Rova <rr@mima.x.se>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License,or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not,write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <limits.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrandr.h>

#include <libtu/rb.h>

#include <ioncore/common.h>
#include <ioncore/eventh.h>
#include <ioncore/global.h>
#include <ioncore/event.h>
#include <ioncore/mplex.h>
#include <ioncore/stacking.h>
#include <ioncore/xwindow.h>
#include <ioncore/../version.h>
#include "xrandr.h"

char mod_xrandr_ion_api_version[]=ION_API_VERSION;

static bool hasXrandR=FALSE;
static int xrr_event_base;
static int xrr_error_base;

Rb_node rotations=NULL;

static int rr2scrrot(int rr)
{
    switch(rr){
    case RR_Rotate_0: return SCREEN_ROTATION_0; 
    case RR_Rotate_90: return SCREEN_ROTATION_90; 
    case RR_Rotate_180: return SCREEN_ROTATION_180; 
    case RR_Rotate_270: return SCREEN_ROTATION_270; 
    default: return SCREEN_ROTATION_0; 
    }
}


static void insrot(int id, int r)
{
    Rb_node node;
        
    node=rb_inserti(rotations, id, NULL);
        
    if(node!=NULL)
        node->v.ival=r;
}

/*
 * Put a WScreen on each monitor
 */
void init_screens()
{
    int screencount;
    int screen;
    int existingscreencount = 0;
    WRootWin* rootWin = ioncore_g.rootwins;
    struct xrandr_output_info** output_infos = xrandr_init(ioncore_g.dpy, "ion display", &screencount);
    WMPlexIterTmp tmp;
    WRegion *reg;
    
    if (output_infos == NULL)
        return;

    fprintf(stderr, "screen count: %d\n", screencount);

    FOR_ALL_MANAGED_BY_MPLEX(&rootWin->scr.mplex, reg, tmp){
        existingscreencount++;
        fprintf(stderr, "One managed before at %p\n", reg);
    }

    // disregard the last one - this is probably the hidden root screen?
    if (existingscreencount > 0)
        existingscreencount--;

    for (screen = 0; screen < screencount; screen++){
        struct xrandr_output_info* output_info = output_infos[screen];
        
        //WRegion *existingScreen = mplex_mx_nth(&rootWin->scr.mplex, screen);
        if (screen < existingscreencount) {
            fprintf(stderr, "One existing screen %d\n", screen);
        } else {
            WScreen* newScreen;
            WFitParams fp;
            WMPlexAttachParams par = MPLEXATTACHPARAMS_INIT;
            fprintf(stderr, "One new screen %d\n", screen);

            fp.g.x = output_info->x;
            fp.g.y = output_info->y;
            fp.g.w = output_info->w;
            fp.g.h = output_info->h;
            fp.mode = REGION_FIT_EXACT;
            fprintf(stderr, "Size: %dx%d at %dx%d\n", fp.g.w, fp.g.h, fp.g.x, fp.g.y);
    
            par.flags = MPLEX_ATTACH_GEOM|MPLEX_ATTACH_SIZEPOLICY|MPLEX_ATTACH_UNNUMBERED;
            par.geom = fp.g;
            par.szplcy = SIZEPOLICY_FULL_EXACT;

            newScreen = (WScreen*) mplex_do_attach_new(&rootWin->scr.mplex, &par,
                (WRegionCreateFn*)create_screen, NULL);
            newScreen->id = screen;
        }
    }

    FOR_ALL_MANAGED_BY_MPLEX(&rootWin->scr.mplex, reg, tmp){
        existingscreencount++;
        fprintf(stderr, "One now managed at %p\n", reg);
    }

    rootWin->scr.id = -2;
}

void update_screens()
{
    init_screens();
}

bool handle_xrandr_event(XEvent *ev)
{
    if(hasXrandR && ev->type == xrr_event_base + RRScreenChangeNotify) {
        XRRScreenChangeNotifyEvent *rev=(XRRScreenChangeNotifyEvent *)ev;
        
        WFitParams fp;
        WScreen *screen;
        bool pivot=FALSE;

        update_screens();
        
        screen=XWINDOW_REGION_OF_T(rev->root, WScreen);
        
        if(screen!=NULL){
            int r;
            Rb_node node;
            int found;
            
            r=rr2scrrot(rev->rotation);
            
            fp.g.x=REGION_GEOM(screen).x;
            fp.g.y=REGION_GEOM(screen).y;
            
            if(rev->rotation==RR_Rotate_90 || rev->rotation==RR_Rotate_270){
                fp.g.w=rev->height;
                fp.g.h=rev->width;
            }else{
                fp.g.w=rev->width;
                fp.g.h=rev->height;
            }
            
            fp.mode=REGION_FIT_EXACT;
            
            node=rb_find_ikey_n(rotations, screen->id, &found);
            
            if(!found){
                insrot(screen->id, r);
            }else if(r!=node->v.ival){
                int or=node->v.ival;
                
                fp.mode|=REGION_FIT_ROTATE;
                fp.rotation=(r>or
                             ? SCREEN_ROTATION_0+r-or
                             : (SCREEN_ROTATION_270+1)+r-or);
                node->v.ival=r;
            }
            
            REGION_GEOM(screen)=fp.g;
            
            mplex_managed_geom((WMPlex*)screen, &(fp.g));
            
            mplex_do_fit_managed((WMPlex*)screen, &fp);
        }
        
        return TRUE;
    }
    return FALSE;
}




static bool check_pivots()
{
    WScreen *scr;
    XRRScreenConfiguration *cfg;
    
    rotations=make_rb();
    
    if(rotations==NULL)
        return FALSE;
    
    FOR_ALL_SCREENS(scr){
        Rotation rot=RR_Rotate_90;
        
        /* XRRRotations appears to segfault on other screens (id's -2 (the 
         * invisible root screen) and 1 (the second monitor)), on which 
         */
        if (scr->id == 0){
            XRRRotations(ioncore_g.dpy, scr->id, &rot);
            insrot(scr->id, rr2scrrot(rot));
        }
    }
    
    return TRUE;
}



bool mod_xrandr_init()
{
    hasXrandR=
        XRRQueryExtension(ioncore_g.dpy,&xrr_event_base,&xrr_error_base);
        
    if(!check_pivots())
        return FALSE;
    
    if(hasXrandR){
        XRRSelectInput(ioncore_g.dpy,ioncore_g.rootwins->dummy_win,
                       RRScreenChangeNotifyMask);
        init_screens();
    }else{
        warn_obj("mod_xrandr","XRandR is not supported on this display");
    }
    
    hook_add(ioncore_handle_event_alt,(WHookDummy *)handle_xrandr_event);
    
    return TRUE;
}


bool mod_xrandr_deinit()
{
    hook_remove(ioncore_handle_event_alt,
                (WHookDummy *)handle_xrandr_event);
    
    return TRUE;
}
