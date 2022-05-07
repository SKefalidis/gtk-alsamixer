/*
 *  (gtk-alsamixer) An ALSA mixer for GTK
 *
 *  Copyright (C) 2001-2005 Derrick J Houy <djhouy@paw.za.org>.
 *  Copyright (C) 2022 Sergios - Anestis Kefalidis <sergioskefalidis@gmail.com>.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n.h>

#include "gam-app.h"
#include "gam-mixer.h"
#include "gam-prefs-dlg.h"

struct _GamAppPrivate
{
    GtkWidget      *notebook;
};

static gboolean  gam_app_delete                        (GtkWidget             *widget,
                                                        gpointer               user_data);
static void      gam_app_destroy                       (GtkWidget             *widget);
static GObject  *gam_app_constructor                   (GType                  type,
                                                        guint                  n_construct_properties,
                                                        GObjectConstructParam *construct_params);
//static void      gam_app_load_prefs                    (GamApp                *gam_app);
//static void      gam_app_save_prefs                    (GamApp                *gam_app);
static void      gam_app_mixer_display_name_changed_cb (GamMixer              *gam_mixer,
                                                        GamApp                *gam_app);
static void gam_app_mixer_visibility_changed_cb (GamMixer *gam_mixer);

static gpointer parent_class;

G_DEFINE_TYPE_WITH_CODE (GamApp , gam_app, GTK_TYPE_WINDOW,
                         G_ADD_PRIVATE (GamApp))

static void
gam_app_class_init (GamAppClass *klass)
{
    GObjectClass   *gobject_class;
    GtkWidgetClass *widget_class;

    gobject_class = G_OBJECT_CLASS (klass);
    widget_class = GTK_WIDGET_CLASS (klass);

    parent_class = g_type_class_peek_parent (klass);

    gobject_class->constructor = gam_app_constructor;

    widget_class->destroy = gam_app_destroy;
}

static void
gam_app_init (GamApp *gam_app)
{
    g_return_if_fail (GAM_IS_APP (gam_app));

    gam_app->priv = gam_app_get_instance_private (gam_app);
    gam_app->priv->notebook = gtk_notebook_new ();
    gtk_notebook_set_scrollable (GTK_NOTEBOOK (gam_app->priv->notebook), TRUE);
    gtk_notebook_set_tab_pos (GTK_NOTEBOOK (gam_app->priv->notebook), GTK_POS_TOP);
}

static gboolean
gam_app_delete (GtkWidget *widget, gpointer user_data)
{
    GamApp *gam_app;

    g_return_val_if_fail (widget != NULL, FALSE);
    g_return_val_if_fail (GAM_IS_APP (widget), FALSE);

    gam_app = GAM_APP (widget);

//    gam_app_save_prefs (gam_app);

    return FALSE;
}

static void
gam_app_destroy (GtkWidget *widget)
{
    GamApp *gam_app;

    g_return_if_fail (widget != NULL);
    g_return_if_fail (GAM_IS_APP (widget));

    gam_app = GAM_APP (widget);

    gtk_main_quit ();

    gam_app->priv->notebook = NULL;
}

static GObject *
gam_app_constructor (GType                  type,
                     guint                  n_construct_properties,
                     GObjectConstructParam *construct_params)
{
    GObject *object;
    GamApp *gam_app;
    GtkWidget *main_box, *mixer, *label;
    snd_ctl_t *ctl_handle;
    gint result, index = 0;
    gchar *card;

    object = (* G_OBJECT_CLASS (parent_class)->constructor) (type,
                                                             n_construct_properties,
                                                             construct_params);

    gam_app = GAM_APP (object);

    gtk_window_set_default_size (GTK_WINDOW (gam_app), 800, 400);

    g_signal_connect (G_OBJECT (gam_app), "delete_event",
                      G_CALLBACK (gam_app_delete), NULL);

    do {
        card = g_strdup_printf ("hw:%d", index++);

        result = snd_ctl_open (&ctl_handle, card, 0);

        if (result == 0) {
            snd_ctl_close(ctl_handle);

            mixer = gam_mixer_new (gam_app, card);

            if (gam_mixer_get_visible (GAM_MIXER (mixer)))
                gtk_widget_show (mixer);

            g_signal_connect (G_OBJECT (mixer), "display_name_changed",
                              G_CALLBACK (gam_app_mixer_display_name_changed_cb), gam_app);

            g_signal_connect (G_OBJECT (mixer), "visibility_changed",
                              G_CALLBACK (gam_app_mixer_visibility_changed_cb), gam_app);

            label = gtk_label_new (gam_mixer_get_display_name (GAM_MIXER (mixer)));
            gtk_label_set_justify (GTK_LABEL (label), GTK_JUSTIFY_LEFT);

            gtk_notebook_append_page (GTK_NOTEBOOK (gam_app->priv->notebook), mixer, label);
        }

        g_free (card);
    } while (result == 0);

    // Pack widgets into window
    main_box = gtk_vbox_new (FALSE, 0);

    gtk_container_add (GTK_CONTAINER (gam_app), main_box);

    gtk_widget_show_all (GTK_WIDGET (main_box));

    gtk_box_pack_start (GTK_BOX (main_box), gam_app->priv->notebook, TRUE, TRUE, 0);

    gtk_widget_show (gam_app->priv->notebook);

//    gam_app_load_prefs (gam_app);

    return object;
}

//static void
//gam_app_load_prefs (GamApp *gam_app)
//{
//    gint height = 0, width = 0;
//
//    g_return_if_fail (GAM_IS_APP (gam_app));
//
//    if ((height != 0) && (width != 0))
//        gtk_window_resize (GTK_WINDOW (gam_app), width, height);
//}
//
//static void
//gam_app_save_prefs (GamApp *gam_app)
//{
//    gint height, width;
//
//    g_return_if_fail (GAM_IS_APP (gam_app));
//
//    gdk_window_get_geometry (GDK_WINDOW (gam_app), NULL, NULL, &width, &height);
//}

static void
gam_app_mixer_display_name_changed_cb (GamMixer *gam_mixer, GamApp *gam_app)
{
    g_return_if_fail (GAM_IS_APP (gam_app));
    g_return_if_fail (GAM_IS_MIXER (gam_mixer));

    gtk_notebook_set_tab_label_text (GTK_NOTEBOOK (gam_app->priv->notebook),
                                     GTK_WIDGET (gam_mixer),
                                     gam_mixer_get_display_name (gam_mixer));
}

static void
gam_app_mixer_visibility_changed_cb (GamMixer *gam_mixer)
{
    if (gam_mixer_get_visible (gam_mixer))
        gtk_widget_show (GTK_WIDGET (gam_mixer));
    else
        gtk_widget_hide (GTK_WIDGET (gam_mixer));
}

GtkWidget *
gam_app_new (void)
{
    return g_object_new (GAM_TYPE_APP,
                         "title", _("Xfce ALSA Mixer"),
                         NULL);
}

void
gam_app_run (GamApp *gam_app)
{
    gtk_widget_show (GTK_WIDGET (gam_app));
    gtk_main ();
}

gint
gam_app_get_mixer_slider_style (void)
{
    return 0;
}

gint
gam_app_get_slider_toggle_style (void)
{
    return 1;
}