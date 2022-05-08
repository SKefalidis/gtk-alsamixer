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

#include <math.h>
#include <alsamixer/volume_mapping.h>
#include <glib/gi18n.h>

#include "gam-slider-dual.h"

struct _GamSliderDualPrivate
{
    gpointer   app;
    GtkWidget *lock_button;
    GtkWidget *vol_slider_left;
    GtkWidget *vol_slider_right;
    GtkAdjustment *vol_adjustment_left;
    GtkAdjustment *vol_adjustment_right;
    gdouble    pan;
    gboolean   refreshing;
};

static void     gam_slider_dual_finalize                      (GObject               *object);
static GObject *gam_slider_dual_constructor                   (GType                  type,
                                                               guint                  n_construct_properties,
                                                               GObjectConstructParam *construct_params);
static gint     gam_slider_dual_get_volume_left               (GamSliderDual         *gam_slider_dual);
static gint     gam_slider_dual_get_volume_right              (GamSliderDual         *gam_slider_dual);
static void     gam_slider_dual_update_volume_left            (GamSliderDual         *gam_slider_dual);
static void     gam_slider_dual_update_volume_right           (GamSliderDual         *gam_slider_dual);
static gint     gam_slider_dual_lock_button_toggled_cb        (GtkWidget             *widget,
                                                               GamSliderDual         *gam_slider_dual);
static gint     gam_slider_dual_volume_left_value_changed_cb  (GtkWidget             *widget,
                                                               GamSliderDual         *gam_slider_dual);
static gint     gam_slider_dual_volume_right_value_changed_cb (GtkWidget             *widget,
                                                               GamSliderDual         *gam_slider_dual);
static void     gam_slider_dual_refresh                       (GamSlider             *gam_slider);
static void     gam_slider_dual_set_pan                       (GamSliderDual         *gam_slider_dual);
static gboolean gam_slider_dual_get_locked                    (GamSliderDual         *gam_slider_dual);
static void     gam_slider_dual_set_locked                    (GamSliderDual         *gam_slider_dual,
                                                               gboolean               locked);

static gpointer parent_class;

G_DEFINE_TYPE_WITH_CODE (GamSliderDual , gam_slider_dual, GAM_TYPE_SLIDER,
                         G_ADD_PRIVATE (GamSliderDual))

static void
gam_slider_dual_class_init (GamSliderDualClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
    GamSliderClass *object_class = GAM_SLIDER_CLASS (klass);

    parent_class = g_type_class_peek_parent (klass);

    gobject_class->finalize = gam_slider_dual_finalize;
    gobject_class->constructor = gam_slider_dual_constructor;

    object_class->refresh = gam_slider_dual_refresh;
}

static void
gam_slider_dual_init (GamSliderDual *gam_slider_dual)
{
    g_return_if_fail (GAM_IS_SLIDER_DUAL (gam_slider_dual));

    gam_slider_dual->priv = gam_slider_dual_get_instance_private (gam_slider_dual);

    g_object_get (G_OBJECT (gam_slider_dual),
                  "app", &gam_slider_dual->priv->app,
                  NULL);

    gam_slider_dual->priv->lock_button = NULL;
    gam_slider_dual->priv->vol_slider_left = NULL;
    gam_slider_dual->priv->vol_slider_right = NULL;
    gam_slider_dual->priv->vol_adjustment_left = NULL;
    gam_slider_dual->priv->vol_adjustment_right = NULL;
    gam_slider_dual->priv->refreshing = FALSE;
}

static void
gam_slider_dual_finalize (GObject *object)
{
    GamSliderDual *gam_slider_dual;
    
    g_return_if_fail (GAM_IS_SLIDER_DUAL (object));

    gam_slider_dual = GAM_SLIDER_DUAL (object);

    g_object_unref (gam_slider_dual->priv->app);

    gam_slider_dual->priv->app = NULL;
    gam_slider_dual->priv->vol_adjustment_left = NULL;
    gam_slider_dual->priv->vol_adjustment_right = NULL;
    gam_slider_dual->priv->vol_slider_left = NULL;
    gam_slider_dual->priv->vol_slider_right = NULL;

    G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GObject *
gam_slider_dual_constructor (GType                  type,
                            guint                  n_construct_properties,
                            GObjectConstructParam *construct_params)
{
    GObject *object;
    GamSliderDual *gam_slider_dual;
    GtkWidget *hbox;

    object = (* G_OBJECT_CLASS (parent_class)->constructor) (type,
                                                             n_construct_properties,
                                                             construct_params);

    gam_slider_dual = GAM_SLIDER_DUAL (object);

    hbox = gtk_hbox_new (FALSE, 0);
    gtk_widget_show (hbox);

    gam_slider_dual->priv->vol_adjustment_left = gtk_adjustment_new (gam_slider_dual_get_volume_left (gam_slider_dual), 0, 100, 1, 5, 1);

    g_signal_connect (G_OBJECT (gam_slider_dual->priv->vol_adjustment_left), "value-changed", G_CALLBACK (gam_slider_dual_volume_left_value_changed_cb), gam_slider_dual);

    gam_slider_dual->priv->vol_slider_left = gtk_vscale_new (GTK_ADJUSTMENT (gam_slider_dual->priv->vol_adjustment_left));
    gtk_range_set_inverted (GTK_RANGE (gam_slider_dual->priv->vol_slider_left), TRUE);
    gtk_widget_show (gam_slider_dual->priv->vol_slider_left);
    gtk_scale_set_draw_value (GTK_SCALE (gam_slider_dual->priv->vol_slider_left), FALSE);

    gtk_box_pack_start (GTK_BOX (hbox), gam_slider_dual->priv->vol_slider_left, TRUE, TRUE, 0);

    if (!snd_mixer_selem_is_playback_mono (gam_slider_get_elem (GAM_SLIDER (gam_slider_dual)))) {
        gam_slider_dual->priv->vol_adjustment_right = gtk_adjustment_new (gam_slider_dual_get_volume_right (gam_slider_dual), 0, 100, 1, 5, 1);

        g_signal_connect (G_OBJECT (gam_slider_dual->priv->vol_adjustment_right), "value-changed", G_CALLBACK (gam_slider_dual_volume_right_value_changed_cb), gam_slider_dual);

        gam_slider_dual->priv->vol_slider_right = gtk_vscale_new (GTK_ADJUSTMENT (gam_slider_dual->priv->vol_adjustment_right));
        gtk_range_set_inverted (GTK_RANGE (gam_slider_dual->priv->vol_slider_right), TRUE);
        gtk_widget_show (gam_slider_dual->priv->vol_slider_right);
        gtk_scale_set_draw_value (GTK_SCALE (gam_slider_dual->priv->vol_slider_right), FALSE);

        gtk_box_pack_start (GTK_BOX (hbox), gam_slider_dual->priv->vol_slider_right, TRUE, TRUE, 0);
    }

    gam_slider_add_volume_widget (GAM_SLIDER (gam_slider_dual), hbox);

    if (!snd_mixer_selem_is_playback_mono (gam_slider_get_elem (GAM_SLIDER (gam_slider_dual)))) {
        if (gam_app_get_slider_toggle_style () == 0)
            gam_slider_dual->priv->lock_button = gtk_toggle_button_new_with_label (_("Lock"));
        else
            gam_slider_dual->priv->lock_button = gtk_check_button_new_with_label (_("Lock"));

        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (gam_slider_dual->priv->lock_button),
                                      gam_slider_dual_get_locked (gam_slider_dual));

        g_signal_connect (G_OBJECT (gam_slider_dual->priv->lock_button), "toggled",
                          G_CALLBACK (gam_slider_dual_lock_button_toggled_cb), gam_slider_dual);
    } else
        gam_slider_dual->priv->lock_button = gtk_label_new (NULL);

    gtk_widget_show (gam_slider_dual->priv->lock_button);

    gam_slider_add_pan_widget (GAM_SLIDER (gam_slider_dual), gam_slider_dual->priv->lock_button);

    gtk_label_set_mnemonic_widget (gam_slider_get_label_widget (GAM_SLIDER (gam_slider_dual)),
                                   gam_slider_dual->priv->vol_slider_left);

    return object;
}

static gint
gam_slider_dual_get_volume_left (GamSliderDual *gam_slider_dual)
{
    gdouble vol;

    if (snd_mixer_selem_has_playback_volume (gam_slider_get_elem (GAM_SLIDER (gam_slider_dual))))
        vol = get_normalized_playback_volume (gam_slider_get_elem (GAM_SLIDER (gam_slider_dual)), SND_MIXER_SCHN_FRONT_LEFT);
    else
        vol = get_normalized_capture_volume (gam_slider_get_elem (GAM_SLIDER (gam_slider_dual)), SND_MIXER_SCHN_FRONT_LEFT);

    return lrint (ceil (vol * 100));
}

static gint
gam_slider_dual_get_volume_right (GamSliderDual *gam_slider_dual)
{
    gdouble vol;

    if (snd_mixer_selem_has_playback_volume (gam_slider_get_elem (GAM_SLIDER (gam_slider_dual))))
        vol = get_normalized_playback_volume (gam_slider_get_elem (GAM_SLIDER (gam_slider_dual)), SND_MIXER_SCHN_FRONT_RIGHT);
    else
        vol = get_normalized_capture_volume (gam_slider_get_elem (GAM_SLIDER (gam_slider_dual)), SND_MIXER_SCHN_FRONT_RIGHT);

    return lrint (ceil (vol * 100));
}

static void
gam_slider_dual_update_volume_left (GamSliderDual *gam_slider_dual)
{
    gdouble vol_value;

    /* get values */
    if (gam_slider_dual->priv->vol_adjustment_left)
        vol_value = gtk_adjustment_get_value (GTK_ADJUSTMENT (gam_slider_dual->priv->vol_adjustment_left)) / 100;
    else
        vol_value = 0;

    /* set volume */
    if (snd_mixer_selem_has_playback_volume (gam_slider_get_elem (GAM_SLIDER (gam_slider_dual)))) {
        set_normalized_playback_volume (gam_slider_get_elem (GAM_SLIDER (gam_slider_dual)), SND_MIXER_SCHN_FRONT_LEFT, vol_value, 1);
    } else {
        set_normalized_capture_volume (gam_slider_get_elem (GAM_SLIDER (gam_slider_dual)), SND_MIXER_SCHN_FRONT_LEFT, vol_value, 1);
    }
}

static void
gam_slider_dual_update_volume_right (GamSliderDual *gam_slider_dual)
{
    gdouble vol_value;

    /* get values */
    if (gam_slider_dual->priv->vol_adjustment_left)
        vol_value = gtk_adjustment_get_value (GTK_ADJUSTMENT (gam_slider_dual->priv->vol_adjustment_right)) / 100;
    else
        vol_value = 0;

    /* set volume */
    if (snd_mixer_selem_has_playback_volume (gam_slider_get_elem (GAM_SLIDER (gam_slider_dual)))) {
        set_normalized_playback_volume (gam_slider_get_elem (GAM_SLIDER (gam_slider_dual)), SND_MIXER_SCHN_FRONT_RIGHT, vol_value, 1);
    } else {
        set_normalized_capture_volume (gam_slider_get_elem (GAM_SLIDER (gam_slider_dual)), SND_MIXER_SCHN_FRONT_RIGHT, vol_value, 1);
    }
}

static gint
gam_slider_dual_lock_button_toggled_cb (GtkWidget *widget, GamSliderDual *gam_slider_dual)
{
    gam_slider_dual_set_locked (gam_slider_dual,
                                gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (gam_slider_dual->priv->lock_button)));

    if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (gam_slider_dual->priv->lock_button)))
        gam_slider_dual_set_pan (gam_slider_dual);

    return TRUE;
}

static gint
gam_slider_dual_volume_left_value_changed_cb (GtkWidget *widget, GamSliderDual *gam_slider_dual)
{
    if (gam_slider_dual->priv->refreshing)
        return TRUE;

    gam_slider_dual_update_volume_left (gam_slider_dual);

    if (!snd_mixer_selem_is_playback_mono (gam_slider_get_elem (GAM_SLIDER (gam_slider_dual)))) {
        if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (gam_slider_dual->priv->lock_button))) {
            gtk_adjustment_set_value (GTK_ADJUSTMENT (gam_slider_dual->priv->vol_adjustment_right),
                                      gtk_adjustment_get_value (GTK_ADJUSTMENT (gam_slider_dual->priv->vol_adjustment_left)) -
                                      gam_slider_dual->priv->pan);

            gam_slider_dual_update_volume_right (gam_slider_dual);
            gam_slider_dual_set_pan (gam_slider_dual);
        }
    }

    return TRUE;
}

static gint
gam_slider_dual_volume_right_value_changed_cb (GtkWidget *widget, GamSliderDual *gam_slider_dual)
{
    if (gam_slider_dual->priv->refreshing)
        return TRUE;

    gam_slider_dual_update_volume_right (gam_slider_dual);

    if (!snd_mixer_selem_is_playback_mono (gam_slider_get_elem (GAM_SLIDER (gam_slider_dual)))) {
        if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (gam_slider_dual->priv->lock_button))) {
            gtk_adjustment_set_value (GTK_ADJUSTMENT (gam_slider_dual->priv->vol_adjustment_left),
                                      gtk_adjustment_get_value (GTK_ADJUSTMENT (gam_slider_dual->priv->vol_adjustment_right)) +
                                      gam_slider_dual->priv->pan);

            gam_slider_dual_update_volume_left (gam_slider_dual);
            gam_slider_dual_set_pan (gam_slider_dual);
        }
    }

    return TRUE;
}

static void
gam_slider_dual_refresh (GamSlider *gam_slider)
{
    GamSliderDual * const gam_slider_dual = GAM_SLIDER_DUAL (snd_mixer_elem_get_callback_private (gam_slider_get_elem (gam_slider)));

    gam_slider_dual->priv->refreshing = TRUE;

    /* disconnect the signal, otherwise a value change outside the app causes a refresh, which sets the value, which calls the callback,
     * which in turn rounds the value and sets it again system-wide
     */
    g_signal_handlers_disconnect_by_data (G_OBJECT (gam_slider_dual->priv->vol_adjustment_left), gam_slider_dual);
    gtk_adjustment_set_value (GTK_ADJUSTMENT (gam_slider_dual->priv->vol_adjustment_left), (gdouble) gam_slider_dual_get_volume_left (gam_slider_dual));
    g_signal_connect (G_OBJECT (gam_slider_dual->priv->vol_adjustment_left), "value-changed", G_CALLBACK (gam_slider_dual_volume_left_value_changed_cb), gam_slider_dual);

    if (!snd_mixer_selem_is_playback_mono (gam_slider_get_elem (gam_slider))) {
        g_signal_handlers_disconnect_by_data (G_OBJECT (gam_slider_dual->priv->vol_adjustment_right), gam_slider_dual);
        gtk_adjustment_set_value (GTK_ADJUSTMENT (gam_slider_dual->priv->vol_adjustment_right), (gdouble) gam_slider_dual_get_volume_right (gam_slider_dual));
        g_signal_connect (G_OBJECT (gam_slider_dual->priv->vol_adjustment_right), "value-changed", G_CALLBACK (gam_slider_dual_volume_right_value_changed_cb), gam_slider_dual);
    }

    gam_slider_dual_set_pan (gam_slider_dual);

    gam_slider_dual->priv->refreshing = FALSE;
}

static void
gam_slider_dual_set_pan (GamSliderDual *gam_slider_dual)
{
    g_return_if_fail (GAM_IS_SLIDER_DUAL (gam_slider_dual));

    if (gam_slider_dual->priv->vol_adjustment_right != NULL)
        gam_slider_dual->priv->pan = gtk_adjustment_get_value (GTK_ADJUSTMENT (gam_slider_dual->priv->vol_adjustment_left)) -
                                     gtk_adjustment_get_value (GTK_ADJUSTMENT (gam_slider_dual->priv->vol_adjustment_right));
}

static gboolean
gam_slider_dual_get_locked (GamSliderDual *gam_slider_dual)
{
    gboolean locked = TRUE;

    g_return_val_if_fail (GAM_IS_SLIDER_DUAL (gam_slider_dual), TRUE);

    return locked;
}

static void
gam_slider_dual_set_locked (GamSliderDual *gam_slider_dual, gboolean locked)
{
    g_return_if_fail (GAM_IS_SLIDER_DUAL (gam_slider_dual));
}

GtkWidget *
gam_slider_dual_new (gpointer elem, GamMixer *gam_mixer, GamApp *gam_app)
{
    g_return_val_if_fail (GAM_IS_MIXER (gam_mixer), NULL);

    return g_object_new (GAM_TYPE_SLIDER_DUAL,
                         "elem", elem,
                         "mixer", gam_mixer,
                         "app", gam_app,
                         NULL);
}

void
gam_slider_dual_set_size_groups (GamSliderDual *gam_slider_dual,
                                 GtkSizeGroup *pan_size_group,
                                 GtkSizeGroup *mute_size_group,
                                 GtkSizeGroup *capture_size_group)
{
    g_return_if_fail (GAM_IS_SLIDER_DUAL (gam_slider_dual));

    gtk_size_group_add_widget (pan_size_group, gam_slider_dual->priv->lock_button);
    gtk_size_group_add_widget (mute_size_group, gam_slider_get_mute_widget (GAM_SLIDER (gam_slider_dual)));
    gtk_size_group_add_widget (capture_size_group, gam_slider_get_capture_widget (GAM_SLIDER (gam_slider_dual)));
}
