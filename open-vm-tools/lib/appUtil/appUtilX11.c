/*********************************************************
 * Copyright (C) 2008 VMware, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation version 2.1 and no later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the Lesser GNU General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA.
 *
 *********************************************************/

/*
 * appUtilX11.c --
 *
 *    Utility functions to retrieve application icons.
 */

#define _GNU_SOURCE 1
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vmware.h"
#include "str.h"
#include "posix.h"
#include "debug.h"

#ifndef GTK2
#error "Gtk 2.0 is required"
#endif

#include <glib.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <gdk-pixbuf-xlib/gdk-pixbuf-xlib.h>

/*
 *-----------------------------------------------------------------------------
 *
 * AppUtilIconThemeReallyHasIcon --
 *
 *      Utility function to detect whether an icon is really available. This is necessary
 *      because sometimes gtk_icon_theme_has_icon() lies to us...
 *
 * Results:
 *      TRUE if the icon theme really has a usable icon, FALSE otherwise.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

static Bool
AppUtilIconThemeReallyHasIcon(GtkIconTheme *iconTheme, // IN
                              const char *iconName)    // IN
{
   gint *iconSizes;
   Bool retval;

   if (!gtk_icon_theme_has_icon(iconTheme, iconName)) {
      return FALSE;
   }

   iconSizes = gtk_icon_theme_get_icon_sizes(iconTheme, iconName);
   retval = iconSizes && iconSizes[0];
   g_free(iconSizes);

   return retval;
}


/*
 *-----------------------------------------------------------------------------
 *
 * AppUtilCollectNamedIcons --
 *
 *      Tries to find icons with a particular name (which may be a full filesystem path,
 *      a filename with extension, or just an abstract app name).
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      May add icons into 'pixbufs'
 *
 *-----------------------------------------------------------------------------
 */

static void
AppUtilCollectNamedIcons(GPtrArray *pixbufs,   // IN/OUT
                         const char *iconName) // IN
{
   char *myIconName;
   /*
    * Use the GtkIconTheme to track down any available icons for this app.
    */
   GtkIconTheme *iconTheme = NULL;
   char *ctmp2;
   Bool foundIt; // Did we find this icon in the GtkIconTheme?
   static const char *extraIconPaths[] = {
      "/usr/share/pixmaps",
      "/usr/local/share/pixmaps",
      "/usr/local/share/icons",
      "/opt/kde3/share/icons",
      "/opt/kde3/share/pixmaps",
      "/opt/kde4/share/icons",
      "/opt/kde4/share/pixmaps",
      "/opt/gnome/share/icons",
      "/opt/gnome/share/pixmaps"
   };
   int iconNameLen;

   ASSERT(iconName);

   iconNameLen = strlen(iconName) + 1;
   myIconName = g_alloca(iconNameLen); // We need to modify the name sometimes
   Str_Strcpy(myIconName, iconName, iconNameLen);

   ctmp2 = strrchr(myIconName, '.');
   if (*myIconName != '/' && ctmp2 && strlen(ctmp2) <= 5) {
      /*
       * If it's a plain filename that we could possibly feed into GtkIconTheme as an
       * icon ID, trim the file extension to turn it into an icon ID string and make
       * GtkIconTheme happy.
       */
      *ctmp2 = '\0';
   }

   iconTheme = gtk_icon_theme_get_default();
   g_object_ref(G_OBJECT(iconTheme));
   foundIt = AppUtilIconThemeReallyHasIcon(iconTheme, myIconName);
   if (!foundIt) {
      /*
       * Try looking through some auxiliary icon themes.
       */
      int i;
      static const char *extraThemes[] = {
         /*
          * Some other icon themes to try.
          */
         "hicolor",
         "Bluecurve",
         "HighContrast-SVG",
         "HighContrastLargePrint",
         NULL
      };
      g_object_unref(G_OBJECT(iconTheme));
      iconTheme = gtk_icon_theme_new();
      for (i = 0; i < ARRAYSIZE(extraIconPaths); i++) {
         if (extraIconPaths[i]) {
            if (g_file_test(extraIconPaths[i], G_FILE_TEST_EXISTS)) {
               gtk_icon_theme_append_search_path(iconTheme, extraIconPaths[i]);
            } else {
               extraIconPaths[i] = NULL;
            }
         }
      }

      for (i = 0; extraThemes[i]; i++) {
         gtk_icon_theme_set_custom_theme(iconTheme, extraThemes[i]);
         foundIt = AppUtilIconThemeReallyHasIcon(iconTheme, myIconName);
         if (foundIt) {
            break;
         }
      }
   }

   if (!foundIt) {
      /*
       * Try looking for it as a non-GtkIconTheme managed file, to deal with older
       * systems.
       */
      if (iconName[0] != '/') {
         char *ctmp2;
         char *ctmpext;
         int i;

         myIconName = NULL;
         ctmpext = strrchr(iconName, '.');
         ctmp2 = g_alloca(PATH_MAX);
         for (i = 0; !myIconName && i < ARRAYSIZE(extraIconPaths); i++) {
            if (!extraIconPaths[i]) {
               continue;
            }

            if (ctmpext) {
               g_snprintf(ctmp2, PATH_MAX, "%s/%s", extraIconPaths[i], iconName);
               if (g_file_test(ctmp2, G_FILE_TEST_EXISTS)) {
                  myIconName = ctmp2;
               }
            } else {
               static const char *iconExtensions[] = {
                  ".png",
                  ".xpm",
                  ".gif",
                  ".svg",
               };
               int j;

               for (j = 0; j < ARRAYSIZE(iconExtensions); j++) {
                  g_snprintf(ctmp2, PATH_MAX, "%s/%s%s",
                             extraIconPaths[i],
                             iconName, iconExtensions[j]);
                  if (g_file_test(ctmp2, G_FILE_TEST_EXISTS)) {
                     myIconName = ctmp2;
                     break;
                  }
               }
            }
         }
      } else {
         Str_Strcpy(myIconName, iconName, iconNameLen);
      }
   }

   if (foundIt) {
      /*
       * If we know this icon can be loaded via GtkIconTheme, do so.
       */
      gint *iconSizes;
      int i;
      Bool needToUseScalable;

      iconSizes = gtk_icon_theme_get_icon_sizes(iconTheme, myIconName);

      ASSERT(iconSizes);
      needToUseScalable = (iconSizes[0] == -1 && iconSizes[1] == 0);

      /*
       * Before we try to actually dump the icons out to the host, count how many we
       * actually can load.
       */
      for (i = 0; iconSizes[i]; i++) {
         GdkPixbuf *pixbuf;
         GtkIconInfo *iconInfo;
         gint thisSize;

         thisSize = iconSizes[i];
         if (thisSize == -1 && !needToUseScalable) {
            continue; // Skip scalable icons if we have prerendered versions
         }

         if (thisSize == -1) {
            thisSize = 64; // Render SVG icons to 64x64
         }

         iconInfo = gtk_icon_theme_lookup_icon(iconTheme, myIconName, thisSize, 0);

         if (!iconInfo) {
            Debug("Couldn't find %s icon for size %d\n", myIconName, thisSize);
            continue;
         }

         pixbuf = gtk_icon_info_load_icon(iconInfo, NULL);

         if (!pixbuf) {
            pixbuf = gtk_icon_info_get_builtin_pixbuf(iconInfo);
         }

         if (pixbuf) {
            g_ptr_array_add(pixbufs, pixbuf);
         } else {
            Debug("WARNING: Not even a built-in pixbuf for icon %s\n", myIconName);
         }

         gtk_icon_info_free(iconInfo);
      }

      g_free(iconSizes);

   } else if (myIconName && myIconName[0] == '/') {
      GdkPixbuf *pixbuf;
      pixbuf = gdk_pixbuf_new_from_file(myIconName, NULL);
      if (pixbuf) {
         g_ptr_array_add(pixbufs, pixbuf);
      }
   }

   if (iconTheme) {
      g_object_unref(G_OBJECT(iconTheme));
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * AppUtilComparePixbufSizes --
 *
 *      Compares two GdkPixbufs to sort them by image dimensions
 *
 * Results:
 *      -1 if A is larger than B, 0 if equal, 1 if A is smaller than B
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

static gint
AppUtilComparePixbufSizes(gconstpointer a, // IN
                          gconstpointer b) // IN
{
   GdkPixbuf *pba;
   GdkPixbuf *pbb;
   int asize;
   int bsize;

   if (a && !b) {
      return -1;
   } else if (!a && b) {
      return 1;
   } else if (!a && !b) {
      return 0;
   }

   pba = GDK_PIXBUF(*(gconstpointer *)a);
   asize = gdk_pixbuf_get_width(pba) * gdk_pixbuf_get_height(pba);

   pbb = GDK_PIXBUF(*(gconstpointer *)b);
   bsize = gdk_pixbuf_get_width(pbb) * gdk_pixbuf_get_height(pbb);

   if (asize > bsize) {
      return -1;
   } else if (asize < bsize) {
      return 1;
   }

   return 0;
}


/*
 *-----------------------------------------------------------------------------
 *
 * AppUtil_CollectIconArray --
 *
 *      Given a variety of information about an application (its icon name, X window ID,
 *      etc.), return an array of GdkPixbufs that represent the icons for that
 *      application.
 *
 * Results:
 *      GPtrArray of GdkPixbufs, or NULL on failure. The returned array may have zero
 *      elements. The array will be sorted by icon size, largest to smallest.
 *
 * Side effects:
 *      Caller becomes owner of the array and pixbufs that are allocated during this
 *      function.
 *
 *-----------------------------------------------------------------------------
 */

GPtrArray *
AppUtil_CollectIconArray(const char *iconName,        // IN
                         unsigned long windowID)      // IN
{
   GPtrArray *pixbufs;

   ASSERT(iconName != NULL || windowID != None);

   pixbufs = g_ptr_array_new();

   if (iconName) {
      AppUtilCollectNamedIcons(pixbufs, iconName);
   }

   if (!pixbufs->len && windowID != None) {
      /*
       * Try loading the icon from the X Window's _NET_WM_ICON/WM_HINTS property.
       */
      Display *dpy;
      XWMHints *wmh;
      Atom actualType = None;
      int actualFormat;
      unsigned long nitems = 0;
      unsigned long bytesLeft;
      XID *value;
      XTextProperty wmIconName;

      dpy = gdk_x11_get_default_xdisplay();
      XGetWindowProperty(dpy, windowID, XInternAtom(dpy, "_NET_WM_ICON", FALSE),
                         0, LONG_MAX, False, XA_CARDINAL,
                         &actualType, &actualFormat, &nitems,
                         &bytesLeft, (unsigned char **)&value);

      if (nitems) {
         /*
          * _NET_WM_ICON: Transform ARGB data into pixbufs...
          */
         int i;

         for (i = 0; i < nitems; ) {
            GdkPixbuf *pixbuf;
            int width;
            int height;
            int x;
            int y;
            int rowstride;
            guchar *pixels;

            ASSERT((nitems - i) >= 2);
            width = value[i];
            height = value[i + 1];
            i += 2;
            pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, width, height);
            if (pixbuf) {
               pixels = gdk_pixbuf_get_pixels(pixbuf);
               rowstride = gdk_pixbuf_get_rowstride(pixbuf);

               for (y = 0; y < height; y++) {
                  for (x = 0; x < width && i < nitems; x++, i++) {
                     guchar *pixel = &pixels[y * rowstride + x * 4];
                     XID currentValue = value[i];

                     /*
                      * Input data: BGRA data (high byte is A, low byte is B -
                      * freedesktop calls this ARGB, but that's not correct).
                      *
                      * Output data: RGBA data.
                      */
                     *pixel = (currentValue >> 16) & 0xFF;
                     *(pixel + 1) = (currentValue >> 8) & 0xFF;
                     *(pixel + 2) = currentValue & 0xFF;
                     *(pixel + 3) = (currentValue >> 24) & 0xFF;
                  }
               }

               g_ptr_array_add(pixbufs, pixbuf);
            } else {
               Debug("gdk_pixbuf_new failed when decoding _NET_WM_ICON\n");
               break;
            }
         }
         XFree(value);
      }
      nitems = 0;
      if (!pixbufs->len &&
          XGetWindowProperty(dpy, windowID, XInternAtom(dpy, "_NET_WM_ICON_NAME", FALSE),
                             0, LONG_MAX, False, XInternAtom(dpy, "UTF8_STRING", FALSE),
                             &actualType, &actualFormat, &nitems,
                             &bytesLeft, (unsigned char **)&value) == Success
          && nitems) {
         /*
          * _NET_WM_ICON_NAME
          */
         AppUtilCollectNamedIcons(pixbufs, (char *)value);
         XFree(value);
      }

      if (!pixbufs->len && XGetWMIconName(dpy, windowID, &wmIconName)) {
         /*
          * WM_ICON_NAME
          */
         AppUtilCollectNamedIcons(pixbufs, wmIconName.value);
         XFree(wmIconName.value);
      }

      if (!pixbufs->len && (wmh = XGetWMHints(dpy, windowID))) {
         /*
          * WM_HINTS
          */
         if (wmh->flags & IconPixmapHint) {
            Window dummyWin;
            int x;
            int y;
            unsigned int width;
            unsigned int height;
            unsigned int border;
            unsigned int depth;
            GdkPixbuf *pixbuf = NULL;

            if (XGetGeometry(dpy, wmh->icon_pixmap, &dummyWin,
                             &x, &y, &width, &height, &border, &depth)) {

               pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, width, height);

               if (!gdk_pixbuf_xlib_get_from_drawable (pixbuf, wmh->icon_pixmap,
                                                       DefaultColormap(dpy, 0),
                                                       DefaultVisual(dpy, 0),
                                                       0, 0, 0, 0, width, height)) {
                  g_object_unref(G_OBJECT(pixbuf));
                  pixbuf = NULL;
               }

               if (pixbuf && (wmh->flags & IconMaskHint)) {
                  /*
                   * Apply the X bitmap mask.
                   */
                  GdkPixbuf *pixbuf_mask;

                  pixbuf_mask =
                     gdk_pixbuf_xlib_get_from_drawable(pixbuf,
                                                       wmh->icon_mask,
                                                       DefaultColormap(dpy, 0),
                                                       DefaultVisual(dpy, 0),
                                                       0, 0, 0, 0,
                                                       width, height);
                  if (pixbuf_mask) {
                     int x;
                     int y;
                     int rowstride;
                     int rowstride_mask;
                     guchar *pixels;
                     guchar *pixels_mask;
                     int depth_mask;
                     int n_channels_mask;

                     pixels = gdk_pixbuf_get_pixels(pixbuf);
                     pixels_mask = gdk_pixbuf_get_pixels(pixbuf_mask);
                     rowstride = gdk_pixbuf_get_rowstride(pixbuf);
                     rowstride_mask = gdk_pixbuf_get_rowstride(pixbuf_mask);
                     depth_mask = gdk_pixbuf_get_bits_per_sample(pixbuf_mask);
                     ASSERT(gdk_pixbuf_get_bits_per_sample(pixbuf) == 8);
                     n_channels_mask = gdk_pixbuf_get_n_channels(pixbuf_mask);

                     for (y = 0; y < height; y++) {
                        guchar *thisrow_mask = pixels_mask + y * rowstride_mask;
                        guchar *thisrow = pixels + y * rowstride;
                        for (x = 0; x < width; x++) {
                           guchar newAlpha = 0xFF;
                           switch(depth_mask) {
                           case 1:
                              newAlpha = thisrow_mask[x * n_channels_mask / 8];
                              newAlpha >>= (x % 8);
                              newAlpha = newAlpha ? 0xFF : 0;
                              break;
                           case 8:
                              /*
                               * For some reason, gdk-pixbuf-xlib turns a monochrome
                               * bitmap into 0/1 values in the blue channel of an RGBA
                               * pixmap.
                               */
                              newAlpha = (thisrow_mask[x * n_channels_mask + 2])
                                 ? 0xFF : 0;
                              break;
                           default:
                              NOT_REACHED();
                              break;
                           }

                           thisrow[x * 4 + 3] = newAlpha;
                        }
                     }
                  }
               }

               if (pixbuf) {
                  g_ptr_array_add(pixbufs, pixbuf);
               }
            }
         }

         XFree(wmh);
      }

      if (!pixbufs->len) {
         /*
          * Last resort - try using the WM_CLASS as an icon name
          */

         XClassHint hints;

         if (XGetClassHint(dpy, windowID, &hints)) {
            if (hints.res_name) {
               AppUtilCollectNamedIcons(pixbufs, hints.res_name);
            }

            XFree(hints.res_name);
            XFree(hints.res_class);
         }
      }
   }

   /*
    * In order to make it easy for AppUtil users to pick the icon they want, we sort them
    * largest-to-smallest.
    */
   g_ptr_array_sort(pixbufs, AppUtilComparePixbufSizes);

   if (!pixbufs->len) {
      Debug("WARNING: No icons found for %s / %#lx\n", iconName, windowID);
   }

   return pixbufs;
}


/*
 *-----------------------------------------------------------------------------
 *
 * AppUtil_FreeIconArray --
 *
 *      Frees the result of AppUtil_CollectIconArray
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Array and its contents are destroyed
 *
 *-----------------------------------------------------------------------------
 */

void
AppUtil_FreeIconArray(GPtrArray *pixbufs) // IN
{
   int i;

   if (!pixbufs) {
      return;
   }

   for (i = 0; i < pixbufs->len; i++) {
      g_object_unref(G_OBJECT(g_ptr_array_index(pixbufs, i)));
   }

   g_ptr_array_free(pixbufs, TRUE);
}
/*
 *-----------------------------------------------------------------------------
 *
 * AppUtil_AppIsSkippable --
 *
 *      Can an executable be ignored for the purposes of determining the path to run an
 *      app with? Usually true for interpreters and the like, for which the script path
 *      should be used instead.
 *
 * Results:
 *      TRUE if the app should be ignored, FALSE otherwise.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

Bool
AppUtil_AppIsSkippable(const char *appName)
{
   static const char *skipAppsList[] = {
      "python",
      "python2.5",
      "python2.4",
      "python2.3",
      "python2.2",
      "perl",
      "getproxy"
   };
   char cbuf[PATH_MAX];
   int i;
   char *ctmp;

   Str_Strcpy(cbuf, appName, sizeof cbuf);
   ctmp = basename(cbuf);

   for (i = 0; i < ARRAYSIZE(skipAppsList); i++) {
      if (!strcmp(ctmp, skipAppsList[i])) {
         return TRUE;
      }
   }

   return FALSE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * AppUtil_CanonicalizeAppName --
 *
 *      Turns the app name (or path) into a full path for the executable.
 *
 * Results:
 *      Path, or NULL if not available
 *
 * Side effects:
 *      Allocated memory is returned 
 *
 *-----------------------------------------------------------------------------
 */

char *
AppUtil_CanonicalizeAppName(const char *appName, // IN
                            const char *cwd)     // IN
{
   char *ctmp;
   ASSERT(appName);

   if (appName[0] == '/') {
      return g_strdup(appName);
   }

   ctmp = g_find_program_in_path(appName);
   if (ctmp) {
      return ctmp;
   }

   if (cwd) {
      char cbuf[PATH_MAX];

      getcwd(cbuf, sizeof cbuf);
      chdir(cwd);
      ctmp = Posix_RealPath(appName);
      chdir(cbuf);

      return ctmp;
   }

   return NULL;
}


/*
 *-----------------------------------------------------------------------------
 *
 * AppUtil_Init --
 *
 *      Initializes the AppUtil library for subsequent use.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Internal state is initialized. Currently this is just gdk-pixbuf-xlib.
 *
 *-----------------------------------------------------------------------------
 */

void
AppUtil_Init(void)
{
   gdk_pixbuf_xlib_init(gdk_x11_get_default_xdisplay(), 0);
}
