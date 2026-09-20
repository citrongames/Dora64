# Application icon artwork

`dora64_icon.xcf` is the original layered GIMP file supplied by the project owner.
The unchanged PNG and ICO exports are in `assets/icons/`.

The Windows ICO contains 16, 32, 48, 64, 128 and 256 pixel images. Linux PNGs
cover those sizes plus 512 pixels. CMake embeds the 256 pixel PNG for the SDL
window icon and the ICO as a Windows executable resource. PNGs are also copied
into build assets for Linux desktop integration. The XCF is not copied to builds.
