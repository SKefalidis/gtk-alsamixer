/*
 *  (gtk-alsamixer) An ALSA mixer for GTK
 *
 *  Copyright (C) 2001-2005 Derrick J Houy <djhouy@paw.za.org>.
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
#include <glib/gi18n.h>
#include <gtk/gtkhseparator.h>

#include "gam-mixer.h"
#include "gam-slider-pan.h"
#include "gam-slider-dual.h"
#include "gam-toggle.h"
#include "gam-props-dlg.h"

enum {
    DISPLAY_NAME_CHANGED,
    VISIBILITY_CHANGED,
    LAST_SIGNAL
};

enum {
    PROP_0,
    PROP_APP,
    PROP_CARD_ID
};

struct _GamMixerPrivate
{
    gpointer      app;

    GtkWidget    *slider_box;
    GtkWidget    *toggle_box;

    GtkSizeGroup *pan_size_group;
    GtkSizeGroup *mute_size_group;
    GtkSizeGroup *capture_size_group;

    GSList       *sliders;
    GSList       *toggles;

    snd_mixer_t  *handle;

    guint         input_id_count;
    guint        *input_ids;

    gchar        *card_id;
    gchar        *card_name;
    gchar        *mixer_name;
    gchar        *mixer_name_config;
};

static void     gam_mixer_finalize           (GObject               *object);
static GObject *gam_mixer_constructor        (GType                  type,
                                              guint                  n_construct_properties,
                                              GObjectConstructParam *construct_params);
static void     gam_mixer_set_property       (GObject               *object,
                                              guint                  prop_id,
                                              const GValue          *value,
                                              GParamSpec            *pspec);
static void     gam_mixer_get_property       (GObject               *object,
                                              guint                  prop_id,
                                              GValue                *value,
                                              GParamSpec            *pspec);
static void     gam_mixer_construct_elements (GamMixer              *gam_mixer);
static void     gam_mixer_construct_sliders  (GamMixer              *gam_mixer);
static void     gam_mixer_refresh            (gpointer               data,
                                              gint                   source,
                                              GdkInputCondition      condition);

static gpointer parent_class;
static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE_WITH_CODE (GamMixer , gam_mixer, GTK_TYPE_VBOX,
                         G_ADD_PRIVATE (GamMixer))

static void
gam_mixer_class_init (GamMixerClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
    GtkObjectClass *object_class = (GtkObjectClass*) klass;

    parent_class = g_type_class_peek_parent (klass);

    gobject_class->finalize = gam_mixer_finalize;
    gobject_class->constructor = gam_mixer_constructor;
    gobject_class->set_property = gam_mixer_set_property;
    gobject_class->get_property = gam_mixer_get_property;

    signals[DISPLAY_NAME_CHANGED] =
        g_signal_new ("display_name_changed",
                      G_OBJECT_CLASS_TYPE (object_class),
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET (GamMixerClass, display_name_changed),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);

    signals[VISIBILITY_CHANGED] =
        g_signal_new ("visibility_changed",
                      G_OBJECT_CLASS_TYPE (object_class),
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET (GamMixerClass, visibility_changed),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);

    g_object_class_install_property (gobject_class,
                                     PROP_APP,
                                     g_param_spec_pointer ("app",
                                                           _("Main Application"),
                                                           _("Main Application"),
                                                           (GParamFlags) (G_PARAM_READWRITE | G_PARAM_CONSTRUCT)));

    g_object_class_install_property (gobject_class,
                                     PROP_CARD_ID,
                                     g_param_spec_string ("card_id",
                                                        _("Card ID"),
                                                        _("ALSA Card ID (usually 'default')"),
                                                        NULL,
                                                        (GParamFlags) (G_PARAM_READWRITE | G_PARAM_CONSTRUCT)));
}

static void
gam_mixer_init (GamMixer *gam_mixer)
{
    GtkWidget *separator, *scrolled_window = NULL;
    GtkObject *hadjustment, *vadjustment;

    g_return_if_fail (GAM_IS_MIXER (gam_mixer));

    gam_mixer->priv = gam_mixer_get_instance_private (gam_mixer);

    gam_mixer->priv->app = NULL;
    gam_mixer->priv->card_id = NULL;
    gam_mixer->priv->card_name = NULL;
    gam_mixer->priv->mixer_name = NULL;
    gam_mixer->priv->mixer_name_config = NULL;
    gam_mixer->priv->handle = NULL;
    gam_mixer->priv->input_id_count = 0;
    gam_mixer->priv->input_ids = NULL;

    gam_mixer->priv->sliders = NULL;
    gam_mixer->priv->toggles = NULL;

    gam_mixer->priv->pan_size_group = gtk_size_group_new (GTK_SIZE_GROUP_BOTH);
    gam_mixer->priv->mute_size_group = gtk_size_group_new (GTK_SIZE_GROUP_BOTH);
    gam_mixer->priv->capture_size_group = gtk_size_group_new (GTK_SIZE_GROUP_BOTH);

    hadjustment = gtk_adjustment_new (0, 0, 101, 5, 5, 5);
    vadjustment = gtk_adjustment_new (0, 0, 101, 5, 5, 5);

    scrolled_window = gtk_scrolled_window_new (GTK_ADJUSTMENT (hadjustment),
                                               GTK_ADJUSTMENT (vadjustment));
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
    gtk_widget_show (scrolled_window);
    gtk_box_pack_start (GTK_BOX (gam_mixer),
                        scrolled_window, TRUE, TRUE, 0);

    gam_mixer->priv->slider_box = gtk_hbox_new (TRUE, 0);
    gtk_widget_show (gam_mixer->priv->slider_box);

    gtk_scrolled_window_add_with_viewport (GTK_SCROLLED_WINDOW (scrolled_window),
                                           gam_mixer->priv->slider_box);

    separator = gtk_hseparator_new ();
    gtk_widget_show (separator);
    gtk_box_pack_start (GTK_BOX (gam_mixer),
                        separator, FALSE, TRUE, 1);

    gam_mixer->priv->toggle_box = gtk_hbox_new (TRUE, 0);
    gtk_widget_show (gam_mixer->priv->toggle_box);
    gtk_box_pack_start (GTK_BOX (gam_mixer),
                        gam_mixer->priv->toggle_box, FALSE, FALSE, 0);
}

static void
gam_mixer_finalize (GObject *object)
{
    GamMixer *gam_mixer = GAM_MIXER (object);
    guint input_id;

    for (input_id = 0; input_id < gam_mixer->priv->input_id_count; ++input_id)
        gtk_input_remove (gam_mixer->priv->input_ids[input_id]);

    g_free (gam_mixer->priv->card_id);
    g_free (gam_mixer->priv->card_name);
    g_free (gam_mixer->priv->mixer_name);
    g_free (gam_mixer->priv->mixer_name_config);
    g_free (gam_mixer->priv->input_ids);

    gam_mixer->priv->handle = NULL;
    gam_mixer->priv->app = NULL;
    gam_mixer->priv->card_id = NULL;
    gam_mixer->priv->card_name = NULL;
    gam_mixer->priv->mixer_name = NULL;
    gam_mixer->priv->input_ids = NULL;
    gam_mixer->priv->slider_box = NULL;
    gam_mixer->priv->toggle_box = NULL;
    gam_mixer->priv->pan_size_group = NULL;
    gam_mixer->priv->mute_size_group = NULL;
    gam_mixer->priv->capture_size_group = NULL;

    g_slist_free (gam_mixer->priv->sliders);
    g_slist_free (gam_mixer->priv->toggles);

    gam_mixer->priv->sliders = NULL;
    gam_mixer->priv->toggles = NULL;

    G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GObject*
gam_mixer_constructor (GType                  type,
                       guint                  n_construct_properties,
                       GObjectConstructParam *construct_params)
{
    snd_ctl_card_info_t *hw_info;
    snd_ctl_t *ctl_handle;
    GObject *object;
    GamMixer *gam_mixer;
    gint err, poll_count, poll_fill_count, input_id;
    guint *input_ids;
    struct pollfd *polls;

    object = (* G_OBJECT_CLASS (parent_class)->constructor) (type,
                                                             n_construct_properties,
                                                             construct_params);

    gam_mixer = GAM_MIXER (object);

    snd_ctl_card_info_alloca (&hw_info);

    err = snd_ctl_open (&ctl_handle, gam_mixer->priv->card_id, 0);
    if (err != 0) return NULL;

    err = snd_ctl_card_info (ctl_handle, hw_info);
    if (err != 0) return NULL;

    snd_ctl_close (ctl_handle);

    err = snd_mixer_open (&gam_mixer->priv->handle, 0);
    if (err != 0) return NULL;

    err = snd_mixer_attach (gam_mixer->priv->handle, gam_mixer->priv->card_id);
    if (err != 0) return NULL;

    err = snd_mixer_selem_register (gam_mixer->priv->handle, NULL, NULL);
    if (err != 0) return NULL;

    err = snd_mixer_load (gam_mixer->priv->handle);
    if (err != 0) return NULL;

    gam_mixer->priv->card_name = g_strdup (snd_ctl_card_info_get_name (hw_info));
    gam_mixer->priv->mixer_name = g_strdup (snd_ctl_card_info_get_mixername (hw_info));

    gam_mixer_construct_elements (gam_mixer);

    poll_count = snd_mixer_poll_descriptors_count (gam_mixer->priv->handle);
    if (poll_count < 0) return NULL;

    polls = g_newa (struct pollfd, poll_count);
    poll_fill_count = snd_mixer_poll_descriptors (gam_mixer->priv->handle, polls, poll_count);
    if (poll_count != poll_fill_count) return NULL;
    
    input_ids = g_new (guint, poll_count);

    for (input_id = 0; input_id < poll_count; ++input_id) {
        GdkInputCondition condition = 0;
        const struct pollfd * const pollfd = &polls[input_id];
        const gint source = pollfd->fd;
        const short events = pollfd->events;

        if (events & POLLIN)  condition |= GDK_INPUT_READ;
        if (events & POLLOUT) condition |= GDK_INPUT_WRITE;
        if (events & POLLPRI) condition |= GDK_INPUT_EXCEPTION;

        input_ids[input_id] = gtk_input_add_full (source, condition, gam_mixer_refresh, 0, gam_mixer, 0);
      }

    gam_mixer->priv->input_ids = input_ids;
    gam_mixer->priv->input_id_count = (guint) poll_count;

    return object;
}

static void
gam_mixer_set_property (GObject      *object,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
    GamMixer *gam_mixer;

    gam_mixer = GAM_MIXER (object);

    switch (prop_id) {
        case PROP_APP:
            gam_mixer->priv->app = g_value_get_pointer (value);
            g_object_notify (G_OBJECT (gam_mixer), "app");
            break;
        case PROP_CARD_ID:
            gam_mixer->priv->card_id = g_strdup (g_value_get_string (value));
            g_object_notify (G_OBJECT (gam_mixer), "card_id");
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void
gam_mixer_get_property (GObject    *object,
                        guint       prop_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
    GamMixer *gam_mixer;

    gam_mixer = GAM_MIXER (object);

    switch (prop_id) {
        case PROP_APP:
            g_value_set_pointer (value, gam_mixer->priv->app);
            break;
        case PROP_CARD_ID:
            g_value_set_string (value, gam_mixer->priv->card_id);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void
gam_mixer_construct_elements (GamMixer *gam_mixer)
{
    GtkWidget *toggle, *vbox = NULL;
    snd_mixer_elem_t *elem;
    gint i = 0;

    gam_mixer_construct_sliders (gam_mixer);

    for (elem = snd_mixer_first_elem (gam_mixer->priv->handle); elem; elem = snd_mixer_elem_next (elem)) {
        if (snd_mixer_selem_is_active (elem)) {
            if (snd_mixer_selem_is_enumerated (elem) == FALSE) {
                /* if element is a switch */
                if (!(snd_mixer_selem_has_playback_volume (elem) || snd_mixer_selem_has_capture_volume (elem))) {
                    if (i % 5 == 0) {
                        vbox = gtk_vbox_new (FALSE, 0);
                        gtk_box_pack_start (GTK_BOX (gam_mixer->priv->toggle_box),
                                            vbox, TRUE, TRUE, 0);
                        gtk_widget_show (vbox);
                    }

                    toggle = gam_toggle_new (elem, gam_mixer, GAM_APP (gam_mixer->priv->app));
                    gtk_box_pack_start (GTK_BOX (vbox),
                                        toggle, FALSE, FALSE, 0);
                    if (gam_toggle_get_visible (GAM_TOGGLE (toggle)))
                        gtk_widget_show (toggle);
                    gam_mixer->priv->toggles = g_slist_append (gam_mixer->priv->toggles, toggle);

                    i++;
                }
            } else {
                // TODO: enumerated controls
            }
        }
    }
}

void
gam_mixer_construct_sliders (GamMixer *gam_mixer)
{
    GtkWidget *slider;
    snd_mixer_elem_t *elem;

    g_return_if_fail (GAM_IS_MIXER (gam_mixer));

    if (gam_mixer->priv->sliders) {
            for (guint i = 0; i < g_slist_length (gam_mixer->priv->sliders); i++) {
                    slider = g_slist_nth_data (gam_mixer->priv->sliders, i);
                    gtk_widget_hide (slider);
                    gtk_container_remove (GTK_CONTAINER (gam_mixer->priv->slider_box), slider);
                }

            g_slist_free (gam_mixer->priv->sliders);
            gam_mixer->priv->sliders = NULL;
        }

    for (elem = snd_mixer_first_elem (gam_mixer->priv->handle); elem; elem = snd_mixer_elem_next (elem)) {
            if (snd_mixer_selem_is_active (elem)) {
                    if (snd_mixer_selem_has_playback_volume (elem) || snd_mixer_selem_has_capture_volume (elem)) {
                            if (gam_app_get_mixer_slider_style () == 1) {
                                    slider = gam_slider_dual_new (elem, gam_mixer, GAM_APP (gam_mixer->priv->app));
                                    gam_slider_dual_set_size_groups (GAM_SLIDER_DUAL (slider),
                                                                     gam_mixer->priv->pan_size_group,
                                                                     gam_mixer->priv->mute_size_group,
                                                                     gam_mixer->priv->capture_size_group);
                                } else {
                                    slider = gam_slider_pan_new (elem, gam_mixer, GAM_APP (gam_mixer->priv->app));
                                    gam_slider_pan_set_size_groups (GAM_SLIDER_PAN (slider),
                                                                    gam_mixer->priv->pan_size_group,
                                                                    gam_mixer->priv->mute_size_group,
                                                                    gam_mixer->priv->capture_size_group);
                                }

                            gtk_box_pack_start (GTK_BOX (gam_mixer->priv->slider_box),
                                                slider, TRUE, TRUE, 0);

                            if (gam_slider_get_visible (GAM_SLIDER (slider)))
                                gtk_widget_show (slider);

                            gam_mixer->priv->sliders = g_slist_append (gam_mixer->priv->sliders, slider);
                        }
                }
        }
}

GtkWidget *
gam_mixer_new (GamApp *gam_app, const gchar *card_id)
{
    return g_object_new (GAM_TYPE_MIXER,
                         "app", gam_app,
                         "card_id", card_id,
                         NULL);
}

const gchar *
gam_mixer_get_mixer_name (GamMixer *gam_mixer)
{
    g_return_val_if_fail (GAM_IS_MIXER (gam_mixer), NULL);

    return gam_mixer->priv->mixer_name;
}

const gchar *
gam_mixer_get_config_name (GamMixer *gam_mixer)
{
    g_return_val_if_fail (GAM_IS_MIXER (gam_mixer), NULL);

//    if (gam_mixer->priv->mixer_name_config == NULL) {
        gam_mixer->priv->mixer_name_config = g_strdup (gam_mixer_get_mixer_name (gam_mixer));
        gam_mixer->priv->mixer_name_config = g_strdelimit (gam_mixer->priv->mixer_name_config, GAM_CONFIG_DELIMITERS, '_');
 //   }

    return gam_mixer->priv->mixer_name_config;
}

gchar *
gam_mixer_get_display_name (GamMixer *gam_mixer)
{
    g_return_val_if_fail (GAM_IS_MIXER (gam_mixer), NULL);

    return g_strdup (gam_mixer_get_mixer_name (gam_mixer));
}

void
gam_mixer_set_display_name (GamMixer *gam_mixer, const gchar *name)
{
    g_return_if_fail (GAM_IS_MIXER (gam_mixer));

    g_signal_emit (G_OBJECT (gam_mixer), signals[DISPLAY_NAME_CHANGED], 0);
}

gboolean
gam_mixer_get_visible (GamMixer *gam_mixer)
{
    gboolean visible = TRUE;

    g_return_val_if_fail (GAM_IS_MIXER (gam_mixer), TRUE);

    return visible;
}

void
gam_mixer_set_visible (GamMixer *gam_mixer, gboolean visible)
{
    g_return_if_fail (GAM_IS_MIXER (gam_mixer));

    g_signal_emit (G_OBJECT (gam_mixer), signals[VISIBILITY_CHANGED], 0);
}

static void
gam_mixer_refresh (gpointer data, gint source, GdkInputCondition condition)
{
    const GamMixer * const gam_mixer = GAM_MIXER (data);

    snd_mixer_handle_events (gam_mixer->priv->handle);
}

void
gam_mixer_show_props_dialog (GamMixer *gam_mixer)
{
    static GtkWidget *dialog = NULL;

    g_return_if_fail (GAM_IS_MIXER (gam_mixer));

    if (dialog != NULL) {
        gtk_window_present (GTK_WINDOW (dialog));
        gtk_window_set_transient_for (GTK_WINDOW (dialog),
                                      GTK_WINDOW (GTK_WINDOW (gam_mixer->priv->app)));

        return;
    }
}

gint
gam_mixer_slider_count (GamMixer *gam_mixer)
{
    g_return_val_if_fail (GAM_IS_MIXER (gam_mixer), 0);

    return g_slist_length (gam_mixer->priv->sliders);
}

gint
gam_mixer_toggle_count (GamMixer *gam_mixer)
{
    g_return_val_if_fail (GAM_IS_MIXER (gam_mixer), 0);

    return g_slist_length (gam_mixer->priv->toggles);
}

GamSlider *
gam_mixer_get_nth_slider (GamMixer *gam_mixer, gint index)
{
    g_return_val_if_fail (GAM_IS_MIXER (gam_mixer), NULL);
    g_return_val_if_fail (gam_mixer->priv->sliders != NULL, NULL);

    return GAM_SLIDER (g_slist_nth_data (gam_mixer->priv->sliders, index));
}

GamToggle *
gam_mixer_get_nth_toggle (GamMixer *gam_mixer, gint index)
{
    g_return_val_if_fail (GAM_IS_MIXER (gam_mixer), NULL);
    g_return_val_if_fail (gam_mixer->priv->toggles != NULL, NULL);

    return GAM_TOGGLE (g_slist_nth_data (gam_mixer->priv->toggles, index));
}
