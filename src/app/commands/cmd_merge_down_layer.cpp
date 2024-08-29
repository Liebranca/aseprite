// Aseprite
// Copyright (C) 2019-2020  Igara Studio S.A.
// Copyright (C) 2001-2018  David Capello
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "app/app.h"
#include "app/cmd/add_cel.h"
#include "app/cmd/replace_image.h"
#include "app/cmd/set_cel_position.h"
#include "app/cmd/set_cel_opacity.h"
#include "app/cmd/unlink_cel.h"
#include "app/commands/command.h"
#include "app/context_access.h"
#include "app/doc.h"
#include "app/doc_api.h"
#include "app/modules/gui.h"
#include "app/tx.h"
#include "doc/blend_internals.h"
#include "doc/cel.h"
#include "doc/image.h"
#include "doc/layer.h"
#include "doc/primitives.h"
#include "doc/sprite.h"
#include "render/rasterize.h"
#include "ui/ui.h"

namespace app {

class MergeDownLayerCommand : public Command {
public:
  MergeDownLayerCommand();

protected:
  bool onEnabled(Context* context) override;
  void onExecute(Context* context) override;
};

MergeDownLayerCommand::MergeDownLayerCommand()
  : Command(CommandId::MergeDownLayer(), CmdRecordableFlag)
{
}

bool MergeDownLayerCommand::onEnabled(Context* context)
{
  if (!context->checkFlags(ContextFlags::ActiveDocumentIsWritable |
                           ContextFlags::HasActiveSprite))
    return false;

  const ContextReader reader(context);
  const Sprite* sprite(reader.sprite());
  if (!sprite)
    return false;

  const Layer* src_layer = reader.layer();
  if (!src_layer ||
      !src_layer->isImage() ||
      src_layer->isTilemap()) // TODO Add support to merge tilemaps (and groups!)
    return false;

  const Layer* dst_layer = src_layer->getPrevious();
  if (!dst_layer ||
      !dst_layer->isImage() ||
      dst_layer->isTilemap()) // TODO Add support to merge tilemaps
    return false;

  return true;
}

void MergeDownLayerCommand::onExecute(Context* context)
{
  ContextWriter writer(context);
  Doc* document(writer.document());
  Sprite* sprite(writer.sprite());
  LayerImage* top_layer = static_cast<LayerImage*>(writer.layer());
  Layer* bottom_layer = top_layer->getPrevious();

  Tx tx(writer, friendlyName(), ModifyDocument);

  for (frame_t frpos = 0; frpos<sprite->totalFrames(); ++frpos) {
    // Get frames
    Cel* src_cel = top_layer->cel(frpos);
    Cel* dst_cel = bottom_layer->cel(frpos);

    // By default, the cel at the top would be source and
    // the bottom one would be destination, so top is then
    // merged into bottom. However, if the topmost cel has
    // a lower z-index, we must invert the merging order
    // while still keeping the original top/bottom distinction
    Cel* bottom_cel = dst_cel;
    LayerImage* src_layer = top_layer;
    Layer* dst_layer = bottom_layer;

    if ((bottom_cel != NULL && src_cel != NULL) &&
        (bottom_cel->zIndex() > src_cel->zIndex())) {
      // Set topmost layer and cel as destination,
      // and bottom layer and cel as source
      Layer* tmp_layer = dst_layer;
      dst_layer = static_cast<Layer*>(src_layer);
      src_layer = static_cast<LayerImage*>(tmp_layer);

      Cel* tmp_cel = dst_cel;
      dst_cel = src_cel;
      src_cel = tmp_cel;

    }

    // Get images
    Image* src_image;
    if (src_cel != NULL)
      src_image = src_cel->image();
    else
      src_image = NULL;

    ImageRef dst_image;
    if (dst_cel)
      dst_image = dst_cel->imageRef();

    // With source image?
    if (src_image) {

      // Do nothing when source is bottom and top is transparent
      if ((src_cel == bottom_cel) && !dst_image)
        continue;

      int t;
      int opacity;
      opacity = MUL_UN8(src_cel->opacity(), src_layer->opacity(), t);

      // No destination image
      if (!dst_image) {  // Only a transparent layer can have a null cel
        // Copy this cel to the destination layer...

        // Creating a copy of the image
        dst_image.reset(
          render::rasterize_with_cel_bounds(src_cel));

        // Creating a copy of the cell
        dst_cel = new Cel(frpos, dst_image);
        dst_cel->setPosition(src_cel->x(), src_cel->y());
        dst_cel->setOpacity(opacity);

        tx(new cmd::AddCel(dst_layer, dst_cel));
      }
      // With destination
      else {
        gfx::Rect bounds;

        // Merge down in the background layer
        if (dst_layer->isBackground()) {
          bounds = sprite->bounds();
        }
        // Merge down in a transparent layer
        else {
          bounds = src_cel->bounds().createUnion(dst_cel->bounds());
        }

        doc::color_t bgcolor = app_get_color_to_clear_layer(dst_layer);

        ImageRef new_image(doc::crop_image(
            dst_image.get(),
            bounds.x-dst_cel->x(),
            bounds.y-dst_cel->y(),
            bounds.w, bounds.h, bgcolor));

        // Draw src_cel on new_image
        render::rasterize(
          new_image.get(), src_cel,
          -bounds.x, -bounds.y, false);

        // First unlink the dst_cel
        if (dst_cel->links())
          tx(new cmd::UnlinkCel(dst_cel));


        // Then modify whichever one of the two cels
        // is at the bottom, regardless of z-index
        tx(new cmd::SetCelPosition(bottom_cel,
            bounds.x, bounds.y));

        tx(new cmd::SetCelOpacity(bottom_cel,
            dst_cel->opacity()));

        tx(new cmd::ReplaceImage(sprite,
            bottom_cel->imageRef(), new_image));
      }
    }
  }

  document->notifyLayerMergedDown(top_layer, bottom_layer);
  document->getApi(tx).removeLayer(top_layer); // top_layer is deleted inside removeLayer()

  tx.commit();

#ifdef ENABLE_UI
  if (context->isUIAvailable())
    update_screen_for_document(document);
#endif
}

Command* CommandFactory::createMergeDownLayerCommand()
{
  return new MergeDownLayerCommand;
}

} // namespace app
