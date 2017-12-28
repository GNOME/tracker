#ifndef __TRACKER_STRING_BUILDER_H__
#define __TRACKER_STRING_BUILDER_H__

#include <glib.h>

typedef struct _TrackerStringBuilder TrackerStringBuilder;

TrackerStringBuilder * tracker_string_builder_new  (void);
void                   tracker_string_builder_free (TrackerStringBuilder *builder);

TrackerStringBuilder * tracker_string_builder_append_placeholder  (TrackerStringBuilder *builder);
TrackerStringBuilder * tracker_string_builder_prepend_placeholder (TrackerStringBuilder *builder);

void tracker_string_builder_append  (TrackerStringBuilder *builder,
                                     const gchar          *string,
                                     gssize                len);
void tracker_string_builder_prepend (TrackerStringBuilder *builder,
                                     const gchar          *string,
                                     gssize                len);
void tracker_string_builder_prepend_valist (TrackerStringBuilder *builder,
                                            const gchar          *format,
                                            va_list               args);
void tracker_string_builder_append_valist  (TrackerStringBuilder *builder,
                                            const gchar          *format,
                                            va_list               args);
void tracker_string_builder_append_printf  (TrackerStringBuilder *builder,
                                            const gchar          *format,
                                            ...);
void tracker_string_builder_prepend_printf (TrackerStringBuilder *builder,
                                            const gchar          *format,
                                            ...);

gchar * tracker_string_builder_to_string (TrackerStringBuilder *builder);

gboolean tracker_string_builder_is_empty (TrackerStringBuilder *builder);

#endif /* __TRACKER_STRING_BUILDER_H__ */
