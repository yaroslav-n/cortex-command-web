# Portable source lists from upstream Meson targets.
add_library(cortex_sdl3_image STATIC
  "${ENGINE}/external/sources/SDL3_image-3.2.4/src/IMG.c"
  "${ENGINE}/external/sources/SDL3_image-3.2.4/src/IMG_WIC.c"
  "${ENGINE}/external/sources/SDL3_image-3.2.4/src/IMG_stb.c"
  "${ENGINE}/external/sources/SDL3_image-3.2.4/src/IMG_bmp.c"
  "${ENGINE}/external/sources/SDL3_image-3.2.4/src/IMG_gif.c"
  "${ENGINE}/external/sources/SDL3_image-3.2.4/src/IMG_lbm.c"
  "${ENGINE}/external/sources/SDL3_image-3.2.4/src/IMG_pcx.c"
  "${ENGINE}/external/sources/SDL3_image-3.2.4/src/IMG_pnm.c"
  "${ENGINE}/external/sources/SDL3_image-3.2.4/src/IMG_qoi.c"
  "${ENGINE}/external/sources/SDL3_image-3.2.4/src/IMG_svg.c"
  "${ENGINE}/external/sources/SDL3_image-3.2.4/src/IMG_tga.c"
  "${ENGINE}/external/sources/SDL3_image-3.2.4/src/IMG_xcf.c"
  "${ENGINE}/external/sources/SDL3_image-3.2.4/src/IMG_xpm.c"
  "${ENGINE}/external/sources/SDL3_image-3.2.4/src/IMG_xv.c"
  "${ENGINE}/external/sources/SDL3_image-3.2.4/src/IMG_avif.c"
  "${ENGINE}/external/sources/SDL3_image-3.2.4/src/IMG_jpg.c"
  "${ENGINE}/external/sources/SDL3_image-3.2.4/src/IMG_jxl.c"
  "${ENGINE}/external/sources/SDL3_image-3.2.4/src/IMG_png.c"
  "${ENGINE}/external/sources/SDL3_image-3.2.4/src/IMG_tif.c"
  "${ENGINE}/external/sources/SDL3_image-3.2.4/src/IMG_webp.c"
)
target_compile_definitions(cortex_sdl3_image PRIVATE SDL_IMAGE_USE_COMMON_BACKEND LOAD_BMP LOAD_GIF LOAD_LBM LOAD_PCX LOAD_PNM LOAD_QOI LOAD_SVG LOAD_TGA LOAD_XCF LOAD_XPM LOAD_XV LOAD_PNG SDL_IMAGE_SAVE_JPG=1 SDL_IMAGE_SAVE_PNG=1 SDL_IMAGE_SAVE_AVIF=1)
add_library(cortex_allegro STATIC
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/allegro.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/blit.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/bmp.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/clip3d.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/clip3df.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/colblend.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/color.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/config.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/datafile.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/dataregi.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/dispsw.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/dither.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/drvlist.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/file.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/fli.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/flood.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/font.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/gfx.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/glyph.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/graphics.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/gsprite.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/inline.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/lbm.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/libc.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/lzss.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/math.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/math3d.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/mixer.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/modesel.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/mouse.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/pcx.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/poly3d.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/polygon.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/quantize.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/quat.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/readbmp.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/rle.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/rotate.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/rsfb.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/scene3d.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/spline.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/stream.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/text.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/tga.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/timer.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/unicode.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/vtable.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/vtable15.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/vtable16.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/vtable24.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/vtable32.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/vtable8.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/unix/usystem.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/unix/ugfxdrv.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/unix/umouse.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/unix/ukeybd.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/unix/ufile.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/unix/utimer.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/unix/uptimer.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/unix/ustimer.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/c/cblit16.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/c/cblit24.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/c/cblit32.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/c/cblit8.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/c/ccpu.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/c/ccsprite.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/c/cgfx15.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/c/cgfx16.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/c/cgfx24.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/c/cgfx32.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/c/cgfx8.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/c/cmisc.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/c/cscan15.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/c/cscan16.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/c/cscan24.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/c/cscan32.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/c/cscan8.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/c/cspr15.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/c/cspr16.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/c/cspr24.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/c/cspr32.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/c/cspr8.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/c/cstretch.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/c/czscan15.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/c/czscan16.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/c/czscan24.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/c/czscan32.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/src/c/czscan8.c"
)
target_compile_definitions(cortex_allegro PRIVATE ALLEGRO_SRC)
add_library(cortex_loadpng STATIC
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/addons/loadpng/loadpng.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/addons/loadpng/regpng.c"
  "${ENGINE}/external/sources/allegro 4.4.3.1-custom/addons/loadpng/savepng.c"
)
target_compile_definitions(cortex_loadpng PRIVATE )

set(ALLEGRO_DIR "${ENGINE}/external/sources/allegro 4.4.3.1-custom")
target_include_directories(cortex_allegro BEFORE PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/runtime/include")
target_include_directories(cortex_allegro PUBLIC "${ALLEGRO_DIR}/include" PRIVATE "${ENGINE}/external/sources/tracy/public")
target_include_directories(cortex_loadpng PUBLIC "${ALLEGRO_DIR}/addons/loadpng" PRIVATE "${ENGINE}/external/sources/tracy/public")
target_link_libraries(cortex_loadpng PUBLIC cortex_allegro cortex_png)
target_include_directories(cortex_sdl3_image PUBLIC "${ENGINE}/external/sources/SDL3_image-3.2.4/include")
target_link_libraries(cortex_sdl3_image PUBLIC SDL3::SDL3-static cortex_png)
target_compile_definitions(cortex_sdl3_image PRIVATE WANT_LIBPNG)

file(GLOB PNG_SOURCES "${ENGINE}/external/sources/libpng-1.6.40/src/png*.c")
add_library(cortex_png STATIC ${PNG_SOURCES})
target_include_directories(cortex_png PUBLIC "${ENGINE}/external/sources/libpng-1.6.40/include")
target_compile_options(cortex_png PUBLIC -sUSE_ZLIB=1)
target_link_options(cortex_png PUBLIC -sUSE_ZLIB=1)

set(MINIZIP_DIR "${ENGINE}/external/sources/minizip-ng-4.0.0/src")
add_library(cortex_minizip STATIC)
foreach(name mz_crypt mz_os mz_strm mz_strm_buf mz_strm_mem mz_strm_split mz_zip mz_zip_rw mz_strm_zlib mz_os_posix mz_strm_os_posix mz_compat)
 target_sources(cortex_minizip PRIVATE "${MINIZIP_DIR}/${name}.c")
endforeach()
target_include_directories(cortex_minizip PUBLIC "${MINIZIP_DIR}")
target_compile_definitions(cortex_minizip PRIVATE HAVE_ZLIB ZLIB_COMPAT MZ_ZIP_NO_CRYPTO)
target_compile_options(cortex_minizip PRIVATE -sUSE_ZLIB=1)
target_link_options(cortex_minizip PUBLIC -sUSE_ZLIB=1)
add_custom_target(engine_dependencies DEPENDS cortex_allegro cortex_loadpng cortex_sdl3_image cortex_minizip)

