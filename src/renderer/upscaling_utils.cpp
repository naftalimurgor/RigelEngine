/* Copyright (C) 2019, Nikolai Wuttke. All rights reserved.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "upscaling_utils.hpp"

#include "base/math_tools.hpp"
#include "data/game_options.hpp"
#include "data/game_traits.hpp"
#include "renderer/renderer.hpp"

#include <algorithm>


namespace rigel::renderer
{

namespace
{

auto asVec(const base::Size<int>& size)
{
  return base::Vec2{size.width, size.height};
}


auto asSize(const base::Vec2& vec)
{
  return base::Size{vec.x, vec.y};
}


base::Size<float>
  determineUsableSize(const float windowWidth, const float windowHeight)
{
  auto quantize = [](const float value) {
    return float(int(value) - int(value) % 8);
  };

  const auto actualAspectRatioIsWiderThanTarget =
    windowWidth / windowHeight > data::GameTraits::aspectRatio;
  if (actualAspectRatioIsWiderThanTarget)
  {
    const auto evenHeight = quantize(windowHeight);
    return {data::GameTraits::aspectRatio * evenHeight, evenHeight};
  }
  else
  {
    return {
      quantize(windowWidth),
      quantize(1.0f / data::GameTraits::aspectRatio * windowWidth)};
  }
}


int determineLowResBufferWidth(
  const Renderer* pRenderer,
  const bool widescreenModeWanted)
{
  if (widescreenModeWanted && canUseWidescreenMode(pRenderer))
  {
    const auto scale = determineViewPort(pRenderer).mScale.x;
    const auto fullWidth = determineWidescreenViewPort(pRenderer).mWidthPx;
    return base::round(fullWidth / scale);
  }

  return data::GameTraits::viewPortWidthPx;
}


void setupRenderingViewport(
  renderer::Renderer* pRenderer,
  const bool perElementUpscaling)
{
  if (perElementUpscaling)
  {
    const auto [offset, size, scale] = renderer::determineViewPort(pRenderer);
    pRenderer->setGlobalScale(scale);
    pRenderer->setGlobalTranslation(offset);
    pRenderer->setClipRect(base::Rect<int>{offset, size});
  }
  else
  {
    pRenderer->setClipRect(base::Rect<int>{{}, data::GameTraits::viewPortSize});
  }
}


void setupPresentationViewport(
  renderer::Renderer* pRenderer,
  const bool perElementUpscaling,
  const bool isWidescreenFrame)
{
  if (perElementUpscaling)
  {
    return;
  }

  const auto info = renderer::determineViewPort(pRenderer);
  pRenderer->setGlobalScale(info.mScale);

  if (isWidescreenFrame)
  {
    const auto offset =
      renderer::determineWidescreenViewPort(pRenderer).mLeftPaddingPx;
    pRenderer->setGlobalTranslation({offset, 0});
  }
  else
  {
    pRenderer->setGlobalTranslation(info.mOffset);
  }
}

} // namespace


ViewPortInfo determineViewPort(const Renderer* pRenderer)
{
  const auto windowWidth = float(pRenderer->windowSize().width);
  const auto windowHeight = float(pRenderer->windowSize().height);

  const auto [usableWidth, usableHeight] =
    determineUsableSize(windowWidth, windowHeight);

  const auto widthScale = usableWidth / data::GameTraits::viewPortWidthPx;
  const auto heightScale = usableHeight / data::GameTraits::viewPortHeightPx;
  const auto offsetX = (windowWidth - usableWidth) / 2.0f;
  const auto offsetY = (windowHeight - usableHeight) / 2.0f;

  return {
    base::Vec2{int(offsetX), int(offsetY)},
    base::Size<int>{int(usableWidth), int(usableHeight)},
    base::Vec2f{widthScale, heightScale}};
}


bool canUseWidescreenMode(const Renderer* pRenderer)
{
  const auto windowWidth = float(pRenderer->windowSize().width);
  const auto windowHeight = float(pRenderer->windowSize().height);
  return windowWidth / windowHeight > data::GameTraits::aspectRatio;
}


WidescreenViewPortInfo determineWidescreenViewPort(const Renderer* pRenderer)
{
  const auto info = determineViewPort(pRenderer);

  const auto windowWidth = pRenderer->windowSize().width;
  const auto tileWidthScaled = data::GameTraits::tileSize * info.mScale.x;
  const auto maxTilesOnScreen = int(windowWidth / tileWidthScaled);

  const auto widthInPixels =
    std::min(base::round(maxTilesOnScreen * tileWidthScaled), windowWidth);
  const auto paddingPixels = pRenderer->windowSize().width - widthInPixels;

  return {maxTilesOnScreen, widthInPixels, paddingPixels / 2};
}


base::Vec2 scaleVec(const base::Vec2& vec, const base::Vec2f& scale)
{
  return base::Vec2{base::round(vec.x * scale.x), base::round(vec.y * scale.y)};
}


base::Extents scaleSize(const base::Extents& size, const base::Vec2f& scale)
{
  return asSize(scaleVec(asVec(size), scale));
}


RenderTargetTexture createFullscreenRenderTarget(
  Renderer* pRenderer,
  const data::GameOptions& options)
{
  if (options.mPerElementUpscalingEnabled)
  {
    return RenderTargetTexture{
      pRenderer, pRenderer->windowSize().width, pRenderer->windowSize().height};
  }
  else
  {
    return RenderTargetTexture{
      pRenderer,
      determineLowResBufferWidth(pRenderer, options.mWidescreenModeOn),
      data::GameTraits::viewPortHeightPx};
  }
}


UpscalingBuffer::UpscalingBuffer(
  Renderer* pRenderer,
  const data::GameOptions& options)
  : mRenderTarget(renderer::createFullscreenRenderTarget(pRenderer, options))
  , mpRenderer(pRenderer)
{
}

[[nodiscard]] base::ScopeGuard
  UpscalingBuffer::bind(const bool perElementUpscaling)
{
  auto saved = mRenderTarget.bind();
  mpRenderer->clear();

  setupRenderingViewport(mpRenderer, perElementUpscaling);
  return saved;
}


void UpscalingBuffer::clear()
{
  const auto saved = mRenderTarget.bindAndReset();
  mpRenderer->clear();
}


void UpscalingBuffer::present(
  const bool currentFrameIsWidescreen,
  const bool perElementUpscaling)
{
  mpRenderer->clear();

  auto saved = renderer::saveState(mpRenderer);
  setupPresentationViewport(
    mpRenderer, perElementUpscaling, currentFrameIsWidescreen);

  mpRenderer->setColorModulation({255, 255, 255, mAlphaMod});
  mRenderTarget.render(0, 0);
  mpRenderer->submitBatch();
}


void UpscalingBuffer::setAlphaMod(const std::uint8_t alphaMod)
{
  mAlphaMod = alphaMod;
}


void UpscalingBuffer::updateConfiguration(const data::GameOptions& options)
{
  mRenderTarget = createFullscreenRenderTarget(mpRenderer, options);
  mpRenderer->setFilteringEnabled(
    mRenderTarget.data(),
    options.mUpscalingFilter == data::UpscalingFilter::Bilinear);
}

} // namespace rigel::renderer
