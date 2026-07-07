#include "views/magic_view.hpp"
#include "assets/assets.h"
#include <lvgl/lvgl_cpp/image.hpp>
#include <lvgl/lvgl_cpp/obj.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <vector>

namespace ir_remote {
namespace {

constexpr int32_t kSpriteWidth                    = 10;
constexpr int32_t kSpriteHeight                   = 12;
constexpr int32_t kPlatformWidth                  = 19;
constexpr size_t kPlatformCount                   = 13;
constexpr uint32_t kJumpMinDurationMs             = 510;
constexpr uint32_t kJumpMaxDurationMs             = 720;
constexpr uint32_t kExitDurationMs                = 760;
constexpr uint32_t kCleanupMs                     = 280;
constexpr int32_t kBottomPadding                  = 29;
constexpr int32_t kTopPlatformY                   = 12;
constexpr int32_t kHorizontalPadding              = 19;
constexpr int32_t kPlatformMinVerticalGap         = 10;
constexpr int32_t kPlatformNeighborVerticalRange  = 22;
constexpr int32_t kPlatformNeighborHorizontalGap  = 32;
constexpr int32_t kRouteMinStepX                  = 22;
constexpr int32_t kRouteMaxStepX                  = 62;
constexpr float kMinGravity                       = 500.0f;
constexpr float kMaxGravity                       = 640.0f;
constexpr float kFallGravity                      = 760.0f;
constexpr float kFallInitialVelocity              = 68.0f;
constexpr float kMinApexExtra                     = 36.0f;
constexpr float kMaxApexExtra                     = 66.0f;
constexpr int32_t kFailureChanceDenominator       = 10;
constexpr int32_t kFailureChanceNumerator         = 4;
constexpr uint32_t kMaskColor                     = 0x000000;
constexpr lv_opa_t kMaskOpacity                   = 200;
constexpr uint32_t kMaskFadeInMs                  = 180;
constexpr uint32_t kMaskFadeOutMs                 = 260;
constexpr std::array<uint32_t, 6> kPlatformColors = {
    0xFED40D, 0x53D671, 0x557AFF, 0xFF6DB3, 0xFF8A3D, 0x30D5C8,
};

struct Point {
    float x = 0.0f;
    float y = 0.0f;
};

struct Platform {
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Image> image;
    int32_t x = 0;
    int32_t y = 0;
    bool fake = false;
};

}  // namespace

struct MagicView::Impl {
    explicit Impl(lv_obj_t* parent) : parent(parent)
    {
        mask = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(parent);
        mask->setSize(lv_pct(100), lv_pct(100));
        mask->setBgColor(lv_color_hex(kMaskColor));
        mask->setBgOpa(kMaskOpacity);
        mask->setBorderWidth(0);
        mask->setPaddingAll(0);
        mask->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
        mask->addFlag(LV_OBJ_FLAG_HIDDEN);
        mask->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

        actor = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Image>(parent);
        actor->setSrc(&image_magic_r);
        actor->setHidden(true);
        actor->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

        for (auto& platform : platforms) {
            platform.image = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Image>(parent);
            platform.image->setSrc(&image_magic_platform);
            platform.image->setHidden(true);
            platform.image->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        }
    }

    lv_obj_t* parent = nullptr;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> mask;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Image> actor;
    std::array<Platform, kPlatformCount> platforms;
    std::vector<Point> path;
    std::vector<size_t> routePlatforms;
    struct Hop {
        Point from;
        Point to;
        float velocityX     = 0.0f;
        float velocityY     = 0.0f;
        float gravity       = kMinGravity;
        uint32_t durationMs = 1;
        bool falling        = false;
    };
    std::vector<Hop> hops;
    uint32_t startMs             = 0;
    uint32_t durationMs          = 0;
    uint32_t fakeDisappearMs     = 0;
    int fakePlatformIndex        = -1;
    bool active                  = false;
    bool facingRight             = true;
    bool fakePlatformDisappeared = false;

    void generate(uint32_t magicSerial)
    {
        if (magicSerial == 0 || !parent || !actor) {
            return;
        }

        const int32_t width  = parentWidth();
        const int32_t height = parentHeight();
        std::mt19937 rng(0x1F1F000Du ^ (magicSerial * 0x9E3779B9u));
        std::uniform_int_distribution<int32_t> xDist(
            kHorizontalPadding, std::max(kHorizontalPadding, width - kHorizontalPadding - kPlatformWidth));
        std::uniform_int_distribution<int32_t> routeStepDist(
            kRouteMinStepX, std::max(kRouteMinStepX, std::min(kRouteMaxStepX, width / 4)));
        std::uniform_int_distribution<int32_t> yJitterDist(-5, 6);
        std::uniform_int_distribution<int32_t> skipDist(1, 3);
        std::uniform_int_distribution<int32_t> directionDist(0, 1);
        std::uniform_int_distribution<int32_t> failureChanceDist(1, kFailureChanceDenominator);
        std::uniform_int_distribution<uint32_t> jumpDurationDist(kJumpMinDurationMs, kJumpMaxDurationMs);
        std::uniform_int_distribution<size_t> platformColorDist(0, kPlatformColors.size() - 1);

        const int32_t minX     = kHorizontalPadding;
        const int32_t maxX     = std::max(minX, width - kHorizontalPadding - kPlatformWidth);
        int32_t routeX         = std::clamp(width / 2 - kPlatformWidth / 2, minX, maxX);
        int32_t routeDirection = directionDist(rng) == 0 ? -1 : 1;

        path.clear();
        routePlatforms.clear();
        hops.clear();
        path.reserve(kPlatformCount + 1);
        routePlatforms.reserve(kPlatformCount);
        hops.reserve(kPlatformCount);
        fakePlatformIndex       = -1;
        fakeDisappearMs         = 0;
        fakePlatformDisappeared = false;

        const int32_t bottomY = std::max(kTopPlatformY + 30, height - kBottomPadding);
        const float averageStep =
            static_cast<float>(bottomY - kTopPlatformY) / static_cast<float>(std::max<size_t>(1, kPlatformCount - 1));

        for (size_t i = 0; i < kPlatformCount; ++i) {
            platforms[i].fake = false;
            if (i == 0) {
                platforms[i].y = bottomY;
            } else if (i + 1 == kPlatformCount) {
                platforms[i].y = kTopPlatformY;
            } else {
                const int32_t idealY =
                    static_cast<int32_t>(std::round(static_cast<float>(bottomY) - averageStep * static_cast<float>(i)));
                const int32_t remainingSlots = static_cast<int32_t>(kPlatformCount - 1 - i);
                const int32_t minY           = kTopPlatformY + remainingSlots * kPlatformMinVerticalGap;
                const int32_t maxY           = platforms[i - 1].y - kPlatformMinVerticalGap;
                platforms[i].y               = std::clamp(idealY + yJitterDist(rng), minY, maxY);
            }

            platforms[i].x = platformXFor(i, xDist, rng);
            platforms[i].image->setHidden(false);
            platforms[i].image->setOpa(0);
            platforms[i].image->setPos(platforms[i].x, platforms[i].y);
            platforms[i].image->setImageRecolor(lv_color_hex(kPlatformColors[platformColorDist(rng)]));
            platforms[i].image->setImageRecolorOpa(LV_OPA_COVER);
            lv_obj_move_foreground(platforms[i].image->raw_ptr());
        }

        platforms[0].x = routeX;
        platforms[0].y = bottomY;
        platforms[0].image->setPos(platforms[0].x, platforms[0].y);
        path.push_back(Point{
            static_cast<float>(platforms[0].x + (kPlatformWidth - kSpriteWidth) / 2),
            static_cast<float>(height + kSpriteHeight + 8),
        });

        size_t routeIndex = 0;
        while (routeIndex < kPlatformCount) {
            Platform& platform = platforms[routeIndex];
            if (routeIndex > 0) {
                int32_t nextX = routeX + routeDirection * routeStepDist(rng);
                if (nextX < minX || nextX > maxX) {
                    routeDirection *= -1;
                    nextX = routeX + routeDirection * routeStepDist(rng);
                } else if (directionDist(rng) == 0) {
                    routeDirection *= -1;
                }
                routeX     = std::clamp(nextX, minX, maxX);
                platform.x = routeX;
                platform.image->setPos(platform.x, platform.y);
            }

            path.push_back(actorPointForPlatform(platform));
            routePlatforms.push_back(routeIndex);
            if (routeIndex + 1 >= kPlatformCount) {
                break;
            }
            routeIndex = std::min(kPlatformCount - 1, routeIndex + static_cast<size_t>(skipDist(rng)));
        }

        const bool shouldFail = routePlatforms.size() >= 4 && failureChanceDist(rng) <= kFailureChanceNumerator;
        if (shouldFail) {
            std::uniform_int_distribution<size_t> fakeRouteDist(2, routePlatforms.size() - 2);
            const size_t fakeRoutePosition = fakeRouteDist(rng);
            fakePlatformIndex              = static_cast<int>(routePlatforms[fakeRoutePosition]);
            platforms[static_cast<size_t>(fakePlatformIndex)].fake = true;
            path.resize(fakeRoutePosition + 2);
            routePlatforms.resize(fakeRoutePosition + 1);

            Point fallPoint = path.back();
            fallPoint.x += routeDirection * 10.0f;
            fallPoint.x = std::clamp(fallPoint.x, 0.0f, static_cast<float>(std::max(0, width - kSpriteWidth)));
            fallPoint.y = static_cast<float>(height + kSpriteHeight + 58);
            path.push_back(fallPoint);
        } else {
            Point exitPoint = path.back();
            exitPoint.x += routeDirection * 12.0f;
            exitPoint.x = std::clamp(exitPoint.x, 0.0f, static_cast<float>(std::max(0, width - kSpriteWidth)));
            exitPoint.y = -static_cast<float>(kSpriteHeight + 18);
            path.push_back(exitPoint);
        }

        buildHops(rng, jumpDurationDist);

        durationMs      = 0;
        fakeDisappearMs = 0;
        for (const auto& hop : hops) {
            if (hop.falling) {
                fakeDisappearMs = durationMs;
            }
            durationMs += hop.durationMs;
        }

        startMs     = lv_tick_get();
        active      = true;
        facingRight = true;
        mask->setHidden(false);
        applyOverlayOpacity(0);
        lv_obj_move_foreground(mask->raw_ptr());
        for (auto& platform : platforms) {
            lv_obj_move_foreground(platform.image->raw_ptr());
        }
        actor->setHidden(false);
        lv_obj_move_foreground(actor->raw_ptr());
        applyState(0);
    }

    void tick(uint32_t nowMs)
    {
        if (!active) {
            return;
        }

        const uint32_t elapsed = nowMs >= startMs ? nowMs - startMs : 0;
        applyOverlayOpacity(elapsed);
        applyFakePlatformState(elapsed);
        if (elapsed >= durationMs + kCleanupMs) {
            hide();
            return;
        }

        applyState(std::min(elapsed, durationMs));
    }

    int32_t parentWidth() const
    {
        lv_obj_update_layout(parent);
        const int32_t width = lv_obj_get_width(parent);
        if (width > 0) {
            return width;
        }
        auto* display = lv_display_get_default();
        return display ? lv_display_get_horizontal_resolution(display) : 320;
    }

    int32_t parentHeight() const
    {
        lv_obj_update_layout(parent);
        const int32_t height = lv_obj_get_height(parent);
        if (height > 0) {
            return height;
        }
        auto* display = lv_display_get_default();
        return display ? lv_display_get_vertical_resolution(display) : 240;
    }

    static Point actorPointForPlatform(const Platform& platform)
    {
        return Point{
            static_cast<float>(platform.x + (kPlatformWidth - kSpriteWidth) / 2),
            static_cast<float>(platform.y - kSpriteHeight),
        };
    }

    int32_t platformXFor(size_t index, std::uniform_int_distribution<int32_t>& xDist, std::mt19937& rng) const
    {
        int32_t bestX       = xDist(rng);
        int32_t bestScore   = -1;
        const int32_t y     = platforms[index].y;
        constexpr int tries = 12;

        for (int attempt = 0; attempt < tries; ++attempt) {
            const int32_t candidate = xDist(rng);
            int32_t score           = 1000;
            for (size_t j = 0; j < index; ++j) {
                const int32_t dy = std::abs(y - platforms[j].y);
                if (dy > kPlatformNeighborVerticalRange) {
                    continue;
                }

                const int32_t dx = std::abs(candidate - platforms[j].x);
                score            = std::min(score, dx);
                if (dx < kPlatformNeighborHorizontalGap) {
                    score -= (kPlatformNeighborHorizontalGap - dx) * 2;
                }
            }

            if (score > bestScore) {
                bestScore = score;
                bestX     = candidate;
            }
        }

        return bestX;
    }

    void applyOverlayOpacity(uint32_t elapsed)
    {
        if (!mask) {
            return;
        }

        uint32_t progress = LV_OPA_COVER;
        if (elapsed < kMaskFadeInMs) {
            progress = (static_cast<uint32_t>(LV_OPA_COVER) * elapsed) / kMaskFadeInMs;
        } else if (elapsed > durationMs) {
            const uint32_t fadeElapsed = std::min(elapsed - durationMs, kMaskFadeOutMs);
            progress = (static_cast<uint32_t>(LV_OPA_COVER) * (kMaskFadeOutMs - fadeElapsed)) / kMaskFadeOutMs;
        }

        const auto platformOpa = static_cast<lv_opa_t>(progress);
        const auto maskOpa     = static_cast<lv_opa_t>((static_cast<uint32_t>(kMaskOpacity) * progress) / LV_OPA_COVER);
        mask->setBgOpa(maskOpa);
        for (auto& platform : platforms) {
            if (platform.image) {
                platform.image->setOpa(platformOpa);
            }
        }
    }

    void applyFakePlatformState(uint32_t elapsed)
    {
        if (fakePlatformIndex < 0 || fakePlatformDisappeared || elapsed < fakeDisappearMs) {
            return;
        }

        auto& platform = platforms[static_cast<size_t>(fakePlatformIndex)];
        if (platform.image) {
            platform.image->setHidden(true);
        }
        fakePlatformDisappeared = true;
    }

    void buildHops(std::mt19937& rng, std::uniform_int_distribution<uint32_t>& fallbackDurationDist)
    {
        std::uniform_real_distribution<float> gravityDist(kMinGravity, kMaxGravity);
        std::uniform_real_distribution<float> apexExtraDist(kMinApexExtra, kMaxApexExtra);
        hops.clear();

        for (size_t i = 0; i + 1 < path.size(); ++i) {
            Hop hop;
            hop.from    = path[i];
            hop.to      = path[i + 1];
            hop.gravity = gravityDist(rng);
            hop.falling = fakePlatformIndex >= 0 && i + 2 == path.size();

            const float rise      = hop.from.y - hop.to.y;
            const float apexExtra = apexExtraDist(rng);
            const float velocityY = -std::sqrt(std::max(1.0f, 2.0f * hop.gravity * (std::max(0.0f, rise) + apexExtra)));
            const float discriminant = std::max(1.0f, velocityY * velocityY - 2.0f * hop.gravity * rise);
            float durationSeconds    = (-velocityY + std::sqrt(discriminant)) / hop.gravity;

            if (hop.falling) {
                hop.gravity           = kFallGravity;
                hop.velocityY         = kFallInitialVelocity;
                const float fallDelta = std::max(1.0f, hop.to.y - hop.from.y);
                durationSeconds =
                    (-hop.velocityY + std::sqrt(hop.velocityY * hop.velocityY + 2.0f * hop.gravity * fallDelta)) /
                    hop.gravity;
            } else if (i + 2 == path.size()) {
                durationSeconds = static_cast<float>(kExitDurationMs) / 1000.0f;
                hop.velocityY =
                    (hop.to.y - hop.from.y - 0.5f * hop.gravity * durationSeconds * durationSeconds) / durationSeconds;
            } else {
                hop.velocityY                  = velocityY;
                const float minDurationSeconds = static_cast<float>(kJumpMinDurationMs) / 1000.0f;
                const float maxDurationSeconds = static_cast<float>(kJumpMaxDurationMs) / 1000.0f;
                if (durationSeconds < minDurationSeconds || durationSeconds > maxDurationSeconds) {
                    durationSeconds = static_cast<float>(fallbackDurationDist(rng)) / 1000.0f;
                    hop.gravity     = 2.0f * (hop.to.y - hop.from.y - hop.velocityY * durationSeconds) /
                                  (durationSeconds * durationSeconds);
                    if (hop.gravity < kMinGravity * 0.65f || hop.gravity > kMaxGravity * 1.55f) {
                        hop.gravity = gravityDist(rng);
                        hop.velocityY =
                            (hop.to.y - hop.from.y - 0.5f * hop.gravity * durationSeconds * durationSeconds) /
                            durationSeconds;
                    }
                }
            }

            hop.durationMs = std::max<uint32_t>(1, static_cast<uint32_t>(std::round(durationSeconds * 1000.0f)));
            hop.velocityX  = (hop.to.x - hop.from.x) / (static_cast<float>(hop.durationMs) / 1000.0f);
            hops.push_back(hop);
        }
    }

    void applyState(uint32_t elapsed)
    {
        if (hops.empty()) {
            return;
        }

        uint32_t hopStart = 0;
        size_t hopIndex   = 0;
        for (; hopIndex < hops.size(); ++hopIndex) {
            const uint32_t segmentEnd = hopStart + hops[hopIndex].durationMs;
            if (elapsed <= segmentEnd) {
                break;
            }
            hopStart = segmentEnd;
        }
        hopIndex = std::min(hopIndex, hops.size() - 1);

        const Hop& hop = hops[hopIndex];
        const float t  = static_cast<float>(elapsed - hopStart) / 1000.0f;
        const Point pos{
            hop.from.x + hop.velocityX * t,
            hop.from.y + hop.velocityY * t + 0.5f * hop.gravity * t * t,
        };

        const bool nextFacingRight = hop.velocityX >= 0.0f;
        if (nextFacingRight != facingRight) {
            facingRight = nextFacingRight;
            actor->setSrc(facingRight ? &image_magic_r : &image_magic_l);
        }

        actor->setHidden(false);
        actor->setPos(static_cast<int32_t>(std::round(pos.x)), static_cast<int32_t>(std::round(pos.y)));
        lv_obj_move_foreground(actor->raw_ptr());
    }

    void hide()
    {
        active = false;
        if (mask) {
            mask->setHidden(true);
        }
        if (actor) {
            actor->setHidden(true);
        }
        for (auto& platform : platforms) {
            if (platform.image) {
                platform.image->setHidden(true);
            }
        }
    }
};

MagicView::MagicView(lv_obj_t* parent) : _impl(std::make_unique<Impl>(parent))
{
}

MagicView::~MagicView() = default;

void MagicView::generate(uint32_t magicSerial)
{
    if (_impl) {
        _impl->generate(magicSerial);
    }
}

void MagicView::tick(uint32_t nowMs)
{
    if (_impl) {
        _impl->tick(nowMs);
    }
}

}  // namespace ir_remote
