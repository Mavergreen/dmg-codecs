cmake_minimum_required(VERSION 3.16)
project(DMGCodecsUpdater LANGUAGES OBJC)
find_package(MavericksShipyard REQUIRED)
# spec: shipyard SKILL.md "Sparkle updater" -- the updater builds with clang22, so an updater-only
#       configure turns the AppleClang gate off
set(MAVERICKS_REQUIRE_APPLECLANG OFF)
include(Mavericks)
include(MavericksSparkle)
mavericks_resolve_version(DC_VERSION)
# spec: updater/ICON-CREDIT.txt -- the generic icon on purpose, with the reason and the exit
mavericks_add_updater_app(
  PRODUCT dmg-codecs
  PRODUCT_NAME "Mavericks DMG Codecs"
  VERSION "${DC_VERSION}"
  LOG_PREFIX "MavDMGCodecsUpdater"
  CONFIRM_TITLE "Mavericks DMG Codecs updated"
  CONFIRM_BODY "DMG Codecs for Mavericks was updated. Disk images you open from now on use the new version."
  ALLOW_GENERIC
)
