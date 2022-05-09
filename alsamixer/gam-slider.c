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

#include <alsamixer/gam-slider.h>

enum {
    PROP_0,
    PROP_ELEM,
    PROP_MIXER,
    PROP_APP
};

enum {
    REFRESH,
    LAST_SIGNAL
};

struct _GamSliderPrivate
{
    gpointer          app;
    gpointer          mixer;
    snd_mixer_elem_t *elem;
    gchar            *name;
    gchar            *name_config;
    GtkWidget        *vbox;
    GtkWidget        *label;
    GtkWidget        *mute_button;
    GtkWidget        *capture_button;
};

static void     gam_slider_finalize                  (GObject               *object);
static GObject *gam_slider_constructor               (GType                  type,
                                                      guint                  n_construct_properties,
                                                      GObjectConstructParam *construct_params);
static void     gam_slider_set_property              (GObject               *object,
                                                      guint                  prop_id,
                                                      const GValue          *value,
                                                      GParamSpec            *pspec);
static void     gam_slider_get_property              (GObject               *object,
                                                      guint                  prop_id,
                                                      GValue                *value,
                                                      GParamSpec            *pspec);
static void     gam_slider_set_elem                  (GamSlider             *gam_slider,
                                                      snd_mixer_elem_t      *elem);
static gint     gam_slider_mute_button_toggled_cb    (GtkWidget             *widget,
                                                      GamSlider             *gam_slider);
static gint     gam_slider_capture_button_toggled_cb (GtkWidget             *widget,
                                                      GamSlider             *gam_slider);
static gint     gam_slider_refresh                   (snd_mixer_elem_t      *elem,
                                                      guint                  mask);
static gint     gam_slider_get_widget_position       (GamSlider             *gam_slider,
                                                      GtkWidget             *widget);

static gpointer parent_class;
static guint    signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE_WITH_CODE (GamSlider, gam_slider, GTK_TYPE_BOX, G_ADD_PRIVATE (GamSlider))

static void
gam_slider_class_init (GamSliderClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    parent_class = g_type_class_peek_parent (klass);

    gobject_class->finalize = gam_slider_finalize;
    gobject_class->constructor = gam_slider_constructor;
    gobject_class->set_property = gam_slider_set_property;
    gobject_class->get_property = gam_slider_get_property;

    signals[REFRESH] =
        g_signal_new ("refresh",
                      G_OBJECT_CLASS_TYPE (gobject_class),
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET (GamSliderClass, refresh),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);

    g_object_class_install_property (gobject_class,
                                     PROP_ELEM,
                                     g_param_spec_pointer ("elem",
                                                           _("Element"),
                                                           _("ALSA mixer element"),
                                                           (GParamFlags) (G_PARAM_READWRITE | G_PARAM_CONSTRUCT)));

    g_object_class_install_property (gobject_class,
                                     PROP_MIXER,
                                     g_param_spec_pointer ("mixer",
                                                           _("Mixer"),
                                                           _("Mixer"),
                                                           (GParamFlags) (G_PARAM_READWRITE | G_PARAM_CONSTRUCT)));

    g_object_class_install_property (gobject_class,
                                     PROP_APP,
                                     g_param_spec_pointer ("app",
                                                           _("Main Application"),
                                                           _("Main Application"),
                                                           (GParamFlags) (G_PARAM_READWRITE | G_PARAM_CONSTRUCT)));
}

static void
gam_slider_init (GamSlider *gam_slider)
{
    g_return_if_fail (GAM_IS_SLIDER (gam_slider));

    gtk_orientable_set_orientation (GTK_ORIENTABLE (gam_slider), GTK_ORIENTATION_VERTICAL);

    gam_slider->priv = gam_slider_get_instance_private (gam_slider);

    gam_slider->priv->elem = NULL;
    gam_slider->priv->app = NULL;
    gam_slider->priv->mixer = NULL;
    gam_slider->priv->vbox = NULL;
    gam_slider->priv->name = NULL;
    gam_slider->priv->name_config = NULL;
    gam_slider->priv->mute_button = NULL;
    gam_slider->priv->capture_button = NULL;
}

static void
gam_slider_finalize (GObject *object)
{
    GamSlider *gam_slider;
    
    g_return_if_fail (GAM_IS_SLIDER (object));

    gam_slider = GAM_SLIDER (object);

    snd_mixer_elem_set_callback (gam_slider->priv->elem, NULL);

    g_free (gam_slider->priv->name);
    g_free (gam_slider->priv->name_config);

    gam_slider->priv->name = NULL;
    gam_slider->priv->name_config = NULL;
    gam_slider->priv->label = NULL;
    gam_slider->priv->mute_button = NULL;
    gam_slider->priv->capture_button = NULL;
    gam_slider->priv->elem = NULL;
    gam_slider->priv->app = NULL;
    gam_slider->priv->mixer = NULL;
    gam_slider->priv->vbox = NULL;

    G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GObject *
gam_slider_constructor (GType                  type,
                        guint                  n_construct_properties,
                        GObjectConstructParam *construct_params)
{
    GObject    *object;
    GamSlider  *gam_slider;
    GtkWidget  *separator;
    gchar      *display_name;
    gint        value;

    object = (* G_OBJECT_CLASS (parent_class)->constructor) (type,
                                                             n_construct_properties,
                                                             construct_params);

    gam_slider = GAM_SLIDER (object);

    gam_slider->priv->vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_show (gam_slider->priv->vbox);

    gtk_box_pack_start (GTK_BOX (gam_slider),
                        gam_slider->priv->vbox, TRUE, TRUE, 0);

    separator = gtk_separator_new (GTK_ORIENTATION_VERTICAL);
    gtk_widget_show (separator);

    gtk_box_pack_start (GTK_BOX (gam_slider),
                        separator, FALSE, TRUE, 0);

    display_name = gam_slider_get_display_name (gam_slider);
    gam_slider->priv->label = gtk_label_new_with_mnemonic (display_name);
    g_free (display_name);
    gtk_widget_show (gam_slider->priv->label);

    gtk_box_pack_start (GTK_BOX (gam_slider->priv->vbox),
                        gam_slider->priv->label, FALSE, TRUE, 0);

    if (snd_mixer_selem_has_playback_switch (gam_slider->priv->elem)) {
        if (gam_app_get_slider_toggle_style () == 0)
            gam_slider->priv->mute_button = gtk_toggle_button_new_with_label (_("Mute"));
        else
            gam_slider->priv->mute_button = gtk_check_button_new_with_label (_("Mute"));

        snd_mixer_selem_get_playback_switch (gam_slider->priv->elem, SND_MIXER_SCHN_FRONT_LEFT, &value);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (gam_slider->priv->mute_button), !value);

        g_signal_connect (G_OBJECT (gam_slider->priv->mute_button), "toggled",
                          G_CALLBACK (gam_slider_mute_button_toggled_cb), gam_slider);
    } else
        gam_slider->priv->mute_button = gtk_label_new (NULL);

    gtk_widget_show (gam_slider->priv->mute_button);
    gtk_box_pack_start (GTK_BOX (gam_slider->priv->vbox),
                        gam_slider->priv->mute_button, FALSE, FALSE, 0);

    if (snd_mixer_selem_has_capture_switch (gam_slider->priv->elem)) {
        if (gam_app_get_slider_toggle_style () == 0)
            gam_slider->priv->capture_button = gtk_toggle_button_new_with_label (_("Rec."));
        else
            gam_slider->priv->capture_button = gtk_check_button_new_with_label (_("Rec."));

        snd_mixer_selem_get_capture_switch (gam_slider->priv->elem, SND_MIXER_SCHN_FRONT_LEFT, &value);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (gam_slider->priv->capture_button), value);

        g_signal_connect (G_OBJECT (gam_slider->priv->capture_button), "toggled",
                          G_CALLBACK (gam_slider_capture_button_toggled_cb), gam_slider);
    } else
        gam_slider->priv->capture_button = gtk_label_new (NULL);

    gtk_widget_show (gam_slider->priv->capture_button);
    gtk_box_pack_start (GTK_BOX (gam_slider->priv->vbox),
                        gam_slider->priv->capture_button, FALSE, FALSE, 0);

    return object;
}

static void
gam_slider_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
    GamSlider *gam_slider;

    gam_slider = GAM_SLIDER (object);

    switch (prop_id) {
        case PROP_ELEM:
            gam_slider_set_elem (gam_slider, g_value_get_pointer (value));
            break;
        case PROP_MIXER:
            gam_slider->priv->mixer = g_value_get_pointer (value);
            g_object_notify (G_OBJECT (gam_slider), "mixer");
            break;
        case PROP_APP:
            gam_slider->priv->app = g_value_get_pointer (value);
            g_object_notify (G_OBJECT (gam_slider), "app");
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void
gam_slider_get_property (GObject     *object,
                         guint        prop_id,
                         GValue      *value,
                         GParamSpec  *pspec)
{
    GamSlider *gam_slider;

    gam_slider = GAM_SLIDER (object);

    switch (prop_id) {
        case PROP_ELEM:
            g_value_set_pointer (value, gam_slider->priv->elem);
            break;
        case PROP_MIXER:
            g_value_set_pointer (value, gam_slider->priv->mixer);
            break;
        case PROP_APP:
            g_value_set_pointer (value, gam_slider->priv->app);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void
gam_slider_set_elem (GamSlider *gam_slider, snd_mixer_elem_t *elem)
{
    g_return_if_fail (GAM_IS_SLIDER (gam_slider));

    if (gam_slider->priv->elem)
        snd_mixer_elem_set_callback (gam_slider->priv->elem, NULL);

    if (elem) {
        snd_mixer_elem_set_callback_private (elem, gam_slider);
        snd_mixer_elem_set_callback (elem, gam_slider_refresh);
    }

    gam_slider->priv->elem = elem;

    g_object_notify (G_OBJECT (gam_slider), "elem");
}

static gint
gam_slider_mute_button_toggled_cb (GtkWidget *widget, GamSlider *gam_slider)
{
    snd_mixer_selem_set_playback_switch_all (gam_slider->priv->elem,
                !gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)));

    return TRUE;
}

static gint
gam_slider_capture_button_toggled_cb (GtkWidget *widget, GamSlider *gam_slider)
{
    snd_mixer_selem_set_capture_switch_all (gam_slider->priv->elem,
                gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)));

    return TRUE;
}

static gint
gam_slider_get_widget_position (GamSlider *gam_slider, GtkWidget *widget)
{
    GValue  value = { 0, };
    gint    position;

    g_return_val_if_fail (GAM_IS_SLIDER (gam_slider), -1);
    g_return_val_if_fail (GTK_IS_WIDGET (widget), -1);

    g_value_init (&value, G_TYPE_INT);

    gtk_container_child_get_property (GTK_CONTAINER (gam_slider->priv->vbox),
                                      widget, "position", &value);

    position = g_value_get_int (&value);

    g_value_unset (&value);

    return position;
}


static gint
gam_slider_refresh (snd_mixer_elem_t *elem, guint mask)
{
    GamSlider * const gam_slider = GAM_SLIDER (snd_mixer_elem_get_callback_private (elem));
    gint value;

    if (snd_mixer_selem_has_playback_switch (gam_slider->priv->elem)) {
        snd_mixer_selem_get_playback_switch (gam_slider->priv->elem, SND_MIXER_SCHN_FRONT_LEFT, &value);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (gam_slider->priv->mute_button), !value);
    }

    if (snd_mixer_selem_has_capture_switch (gam_slider->priv->elem)) {
        snd_mixer_selem_get_capture_switch (gam_slider->priv->elem, SND_MIXER_SCHN_FRONT_LEFT, &value);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (gam_slider->priv->capture_button), value);
    }

    g_signal_emit (gam_slider, signals[REFRESH], 0);
    return 0;
}

const gchar *
gam_slider_get_name (GamSlider *gam_slider)
{
    g_return_val_if_fail (GAM_IS_SLIDER (gam_slider), NULL);

    return snd_mixer_selem_get_name (gam_slider->priv->elem);
    
}

const gchar *
gam_slider_get_config_name (GamSlider *gam_slider)
{
    g_return_val_if_fail (GAM_IS_SLIDER (gam_slider), NULL);

//    if (gam_slider->priv->name_config == NULL) {
        gam_slider->priv->name_config = g_strdup (gam_slider_get_name (gam_slider));
        gam_slider->priv->name_config = g_strdelimit (gam_slider->priv->name_config, GAM_CONFIG_DELIMITERS, '_');
//    }

    return gam_slider->priv->name_config;
}

gchar *
gam_slider_get_display_name (GamSlider *gam_slider)
{
    gchar *disp_name;

    g_return_val_if_fail (GAM_IS_SLIDER (gam_slider), NULL);

    disp_name = g_strndup (gam_slider_get_name (gam_slider), 8);

    return disp_name;
}

void
gam_slider_set_display_name (GamSlider *gam_slider, const gchar *name)
{
    g_return_if_fail (GAM_IS_SLIDER (gam_slider));

    gtk_label_set_text_with_mnemonic (GTK_LABEL (gam_slider->priv->label), name);
}

gboolean
gam_slider_get_visible (GamSlider *gam_slider)
{
    gboolean visible = TRUE;

    g_return_val_if_fail (GAM_IS_SLIDER (gam_slider), TRUE);

    return visible;
}

void
gam_slider_set_visible (GamSlider *gam_slider, gboolean visible)
{
    g_return_if_fail (GAM_IS_SLIDER (gam_slider));

    if (visible)
        gtk_widget_show (GTK_WIDGET (gam_slider));
    else
        gtk_widget_hide (GTK_WIDGET (gam_slider));

}

snd_mixer_elem_t *
gam_slider_get_elem (GamSlider *gam_slider)
{
    g_return_val_if_fail (GAM_IS_SLIDER (gam_slider), NULL);
    g_return_val_if_fail (gam_slider->priv->elem != NULL, NULL);

    return gam_slider->priv->elem;
}

GtkLabel *
gam_slider_get_label_widget (GamSlider *gam_slider)
{
    g_return_val_if_fail (GAM_IS_SLIDER (gam_slider), NULL);

    return GTK_LABEL (gam_slider->priv->label);
}

GtkWidget *
gam_slider_get_mute_widget (GamSlider *gam_slider)
{
    g_return_val_if_fail (GAM_IS_SLIDER (gam_slider), NULL);

    return gam_slider->priv->mute_button;
}

GtkWidget *
gam_slider_get_capture_widget (GamSlider *gam_slider)
{
    g_return_val_if_fail (GAM_IS_SLIDER (gam_slider), NULL);

    return gam_slider->priv->capture_button;
}

GamMixer *
gam_slider_get_mixer (GamSlider *gam_slider)
{
    g_return_val_if_fail (GAM_IS_SLIDER (gam_slider), NULL);
    g_return_val_if_fail (GAM_IS_MIXER (gam_slider->priv->mixer), NULL);

    return gam_slider->priv->mixer;
}

void
gam_slider_add_pan_widget (GamSlider *gam_slider, GtkWidget *widget)
{
    gtk_box_pack_start (GTK_BOX (gam_slider->priv->vbox),
                        widget, FALSE, FALSE, 0);

    gtk_box_reorder_child (GTK_BOX (gam_slider->priv->vbox), widget,
                           gam_slider_get_widget_position (gam_slider, gam_slider->priv->mute_button));
}

void
gam_slider_add_volume_widget (GamSlider *gam_slider, GtkWidget *widget)
{
    gtk_box_pack_start (GTK_BOX (gam_slider->priv->vbox),
                        widget, TRUE, TRUE, 0);

    gtk_box_reorder_child (GTK_BOX (gam_slider->priv->vbox), widget, 1);
}
