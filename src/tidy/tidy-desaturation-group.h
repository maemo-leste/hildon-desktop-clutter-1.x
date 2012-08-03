#ifndef TIDYDESATURATION_H_
#define TIDYDESATURATION_H_

#include <clutter/clutter-group.h>
#include <clutter/clutter-types.h>
#include <cogl/cogl.h>

G_BEGIN_DECLS

#define TIDY_TYPE_DESATURATION_GROUP                  (tidy_desaturation_group_get_type ())
#define TIDY_DESATURATION_GROUP(obj)                (G_TYPE_CHECK_INSTANCE_CAST ((obj), TIDY_TYPE_DESATURATION_GROUP, TidyDesaturationGroup))
#define TIDY_IS_DESATURATION_GROUP(obj)             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TIDY_TYPE_DESATURATION_GROUP))
#define TIDY_DESATURATION_GROUP_CLASS(klass)        (G_TYPE_CHECK_CLASS_CAST ((klass), TIDY_TYPE_DESATURATION_GROUP, TidyDesaturationGroupClass))
#define TIDY_IS_DESATURATION_GROUP_CLASS(klass)     (G_TYPE_CHECK_CLASS_TYPE ((klass), TIDY_TYPE_DESATURATION_GROUP))
#define TIDY_DESATURATION_GROUP_GET_CLASS(obj)      (G_TYPE_INSTANCE_GET_CLASS ((obj), TIDY_TYPE_DESATURATION_GROUP, TidyDesaturationGroupClass))


typedef struct _TidyDesaturationGroup         TidyDesaturationGroup;
typedef struct _TidyDesaturationGroupClass    TidyDesaturationGroupClass;
typedef struct _TidyDesaturationGroupPrivate  TidyDesaturationGroupPrivate;

struct _TidyDesaturationGroup
{
  ClutterGroup          parent;

  TidyDesaturationGroupPrivate  *priv;
};

struct _TidyDesaturationGroupClass
{
  /*< private >*/
  ClutterGroupClass parent_class;
};


GType tidy_desaturation_group_get_type (void) G_GNUC_CONST;
ClutterActor *tidy_desaturation_group_new (void);

void tidy_desaturation_group_desaturate(ClutterActor *desaturation_group);
gboolean tidy_desaturation_group_source_buffered(ClutterActor *desaturation_group);
void tidy_desaturation_group_undo_desaturate(ClutterActor *desaturation_group);


G_END_DECLS

#endif /*TIDYDESATURATION_H_*/
