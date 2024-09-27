// Aseprite
// Copyright (C) 2019  Igara Studio S.A.
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifndef APP_CMD_SET_GRID_TYPE_H_INCLUDED
#define APP_CMD_SET_GRID_TYPE_H_INCLUDED
#pragma once

#include "app/cmd.h"
#include "app/cmd/with_sprite.h"
#include "doc/grid.h"

namespace doc {
  class Sprite;
}

namespace app {
namespace cmd {

  class SetGridType : public Cmd
                    , public WithSprite {
  public:
    SetGridType(doc::Sprite* sprite,
                const doc::Grid::Type type);

  protected:
    void onExecute() override;
    void onUndo() override;
    size_t onMemSize() const override {
      return sizeof(*this);
    }

  private:
    void setGrid(const doc::Grid::Type type);

    doc::Grid::Type m_oldType;
    doc::Grid::Type m_newType;
  };

} // namespace cmd
} // namespace app

#endif
