#ifndef __TIDY_TYPES_H__
#define __TIDY_TYPES_H__

#include <glib-object.h>
#include <clutter/clutter.h>

G_BEGIN_DECLS

#define TIDY_TYPE_PADDING               (tidy_padding_get_type ())

typedef struct _TidyPadding             TidyPadding;

struct _TidyPadding
{
  gint32 top;
  gint32 right;
  gint32 bottom;
  gint32 left;
};

GType tidy_padding_get_type (void) G_GNUC_CONST;

G_END_DECLS

#endif /* __TIDY_TYPES_H__ */
