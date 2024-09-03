// Aseprite
// Copyright (C) 2019-2022  Igara Studio S.A.
// Copyright (C) 2001-2018  David Capello
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "app/cmd/flatten_layers.h"

#include "app/cmd/add_layer.h"
#include "app/cmd/add_cel.h"
#include "app/cmd/configure_background.h"
#include "app/cmd/copy_rect.h"
#include "app/cmd/move_layer.h"
#include "app/cmd/remove_layer.h"
#include "app/cmd/remove_cel.h"
#include "app/cmd/replace_image.h"
#include "app/cmd/set_layer_flags.h"
#include "app/cmd/set_layer_name.h"
#include "app/cmd/set_layer_opacity.h"
#include "app/cmd/set_layer_blend_mode.h"
#include "app/cmd/set_cel_opacity.h"
#include "app/cmd/set_cel_position.h"
#include "app/cmd/set_cel_zindex.h"
#include "app/cmd/unlink_cel.h"
#include "app/doc.h"
#include "app/i18n/strings.h"
#include "app/restore_visible_layers.h"
#include "doc/algorithm/shrink_bounds.h"
#include "doc/cel.h"
#include "doc/layer.h"
#include "doc/primitives.h"
#include "doc/sprite.h"
#include "render/render.h"

namespace app {
namespace cmd {

FlattenLayers::FlattenLayers(doc::Sprite* sprite,
                             const doc::SelectedLayers& layers0,
                             const bool newBlend,
                             const bool mergeDown)
  : WithSprite(sprite)
{
  m_newBlendMethod = newBlend;
  m_mergeDown = mergeDown;
  doc::SelectedLayers layers(layers0);
  layers.removeChildrenIfParentIsSelected();

  m_layerIds.reserve(layers.size());
  for (auto layer : layers)
    m_layerIds.push_back(layer->id());
}

void FlattenLayers::onExecute()
{

  Sprite* sprite = this->sprite();
  auto doc = static_cast<Doc*>(sprite->document());

  // Set of layers to be flattened.
  bool backgroundIsSel = false;
  SelectedLayers layers;
  for (auto layerId : m_layerIds) {
    doc::Layer* layer = doc::get<doc::Layer>(layerId);
    ASSERT(layer);
    layers.insert(layer);
    if (layer->isBackground())
      backgroundIsSel = true;
  }

  LayerList list = layers.toBrowsableLayerList();
  if (list.empty())
    return;                     // Do nothing

  // Create a temporary image.
  ImageRef image(Image::create(sprite->spec()));

  LayerImage* flatLayer;  // The layer onto which everything will be flattened.
  color_t bgcolor;        // The background color to use for flatLayer.
  bool newFlatLayer = false;

  flatLayer = sprite->backgroundLayer();
  if (backgroundIsSel && flatLayer && flatLayer->isVisible()) {
    // There exists a visible background layer, so we will flatten onto that.
    bgcolor = doc->bgColor(flatLayer);
  }
  // Get bottom layer when merging down, but only if
  // we are not flattening into the background layer
  else if (m_mergeDown) {
    flatLayer = static_cast<LayerImage*>(list.front());
    bgcolor = sprite->transparentColor();
  }
  else {
    // Create a new transparent layer to flatten everything onto it.
    flatLayer = new LayerImage(sprite);
    ASSERT(flatLayer->isVisible());
    flatLayer->setName(Strings::layer_properties_flattened());
    newFlatLayer = true;
    bgcolor = sprite->transparentColor();
  }

  render::Render render;
  render.setNewBlend(m_newBlendMethod);
  render.setBgOptions(render::BgOptions::MakeNone());

  {
    // Show only the layers to be flattened so other layers are hidden
    // temporarily.
    RestoreVisibleLayers restore;
    restore.showSelectedLayers(sprite, layers);

    // Copy all frames to the background.
    for (frame_t frame(0); frame<sprite->totalFrames(); ++frame) {
      // Clear the image and render this frame.
      clear_image(image.get(), bgcolor);
      render.renderSprite(image.get(), sprite, frame);

      // Get exact bounds for rendered frame
      gfx::Rect bounds_start = image->bounds();
      gfx::Rect bounds = bounds_start;

      bool shrink=doc::algorithm::shrink_bounds(
        image.get(), image->maskColor(), nullptr,
        bounds_start, bounds);

      // Skip when fully transparent
      Cel* cel = flatLayer->cel(frame);
      if (!shrink) {
        if (!newFlatLayer && cel)
          executeAndAdd(new cmd::RemoveCel(cel));

        continue;
      }

      // Apply shrunk bounds to new image
      ImageRef new_image(doc::crop_image(
        image.get(), bounds, image->maskColor()));

      // Replace image on existing cel
      if (cel) {

        // TODO Keep cel links when possible

        if (cel->links())
          executeAndAdd(new cmd::UnlinkCel(cel));

        ImageRef cel_image = cel->imageRef();
        ASSERT(cel_image);

        // Reset cel properties when flattening in-place
        if (!newFlatLayer) {
          executeAndAdd(new cmd::SetCelOpacity(cel, 255));
          executeAndAdd(new cmd::SetCelPosition(cel, bounds.x, bounds.y));
        }

        // Adjust positive z-index for a non-background flatLayer
        // to account for the number of layers being removed
        if (!backgroundIsSel && cel->zIndex() > 0)
          executeAndAdd(new cmd::SetCelZIndex(cel,
            cel->zIndex() - (list.size() - 1)));

        // Modify destination cel
        executeAndAdd(new cmd::ReplaceImage(sprite, cel_image, new_image));
      }
      // Add new cel on null
      else {

        cel = new Cel(frame, new_image);
        cel->setPosition(bounds.origin());
        cel->setZIndex();

        // No need to undo adding this cel when flattening onto
        // a new layer, as the layer itself would be destroyed,
        // hence the lack of a command
        if (newFlatLayer) {
          flatLayer->addCel(cel);
        }
        else {
          executeAndAdd(new cmd::AddCel(flatLayer, cel));
        }
      }
    }
  }

  // Notify observers when merging down
  if (m_mergeDown)
    doc->notifyLayerMergedDown(list.back(), flatLayer);

  // Add new flatten layer
  if (newFlatLayer) {
    executeAndAdd(new cmd::AddLayer(
      list.front()->parent(), flatLayer, list.front()));

  }
  // Reset layer properties when flattening in-place
  else {
    executeAndAdd(new cmd::SetLayerOpacity(flatLayer, 255));
    executeAndAdd(new cmd::SetLayerBlendMode(
      flatLayer, doc::BlendMode::NORMAL));
  }

  // Delete flattened layers.
  for (Layer* layer : layers) {
    // layer can be == flatLayer when we are flattening on the
    // background layer.
    if (layer != flatLayer) {
      executeAndAdd(new cmd::RemoveLayer(layer));
    }
  }
}

} // namespace cmd
} // namespace app
