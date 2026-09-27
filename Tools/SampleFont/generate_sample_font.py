"""Generates the offline font used by the sample exhibit signs (TextRender).

TextRenderComponent renders only offline (bitmap) fonts, and the engine's
offline fonts have no Japanese glyphs. This script rasterizes the engine's
DroidSansFallback (Apache License 2.0) into an offline distance field UFont.

The offline importer resolves fonts by installed family name through GDI, so
the TTF is registered privately to the editor process before the import.

Run inside the editor (Output Log > Python, or `py "<path>"`):
    py "<project>/Tools/SampleFont/generate_sample_font.py"

To add glyphs, append the characters to jp_chars.txt and run again. An
existing font asset is replaced in place, so signs that reference it keep
working.
"""
import ctypes
import os

import unreal

FONT_ASSET_DIR = '/Game/KawaiiPhysicsSample/Examples/Common/Font'
FONT_ASSET_NAME = 'Font_KPS_Sign'
FONT_FAMILY = 'Droid Sans Fallback'
FONT_HEIGHT = 24.0
FR_PRIVATE = 0x10
# Blank glyphs come out with zero width in distance field mode.
BLANK_GLYPH_ADVANCE = {0x20: 8, 0x3000: 24}

_HERE = os.path.dirname(os.path.abspath(__file__))


def _engine_font_path() -> str:
    return os.path.join(
        unreal.Paths.convert_relative_path_to_full(unreal.Paths.engine_content_dir()),
        'Slate', 'Fonts', 'DroidSansFallback.ttf')


def _read_chars() -> str:
    with open(os.path.join(_HERE, 'jp_chars.txt'), encoding='utf-8') as f:
        return f.read().strip()


def _make_factory(chars: str) -> unreal.TrueTypeFontFactory:
    options = unreal.FontImportOptions()
    data = options.get_editor_property('data')
    data.set_editor_property('font_name', FONT_FAMILY)
    data.set_editor_property('height', FONT_HEIGHT)
    # The default TextRender material expects a distance field font (same as the
    # engine's RobotoDistanceField). The importer rasterizes every glyph at
    # Height * ScaleFactor into one intermediate bitmap, so a large scale factor
    # with thousands of glyphs overflows it and crashes the editor.
    data.set_editor_property('enable_antialiasing', False)
    data.set_editor_property('use_distance_field_alpha', True)
    data.set_editor_property('distance_field_scale_factor', 4)
    data.set_editor_property('include_ascii_range', True)
    data.set_editor_property('chars', chars)
    data.set_editor_property('texture_page_width', 2048)
    data.set_editor_property('texture_page_max_height', 2048)
    data.set_editor_property('x_padding', 2)
    data.set_editor_property('y_padding', 2)
    options.set_editor_property('data', data)

    factory = unreal.TrueTypeFontFactory()
    factory.set_editor_property('import_options', options)
    return factory


def _fix_blank_glyphs(font: unreal.Font, chars: str) -> None:
    # Characters is indexed by code point below 256 and by import order above it.
    characters = font.get_editor_property('characters')
    high_chars = sorted({c for c in chars if ord(c) >= 256})
    for code, advance in BLANK_GLYPH_ADVANCE.items():
        if code < 256:
            index = code
        elif chr(code) in high_chars:
            index = 256 + high_chars.index(chr(code))
        else:
            continue
        if index >= len(characters):
            continue
        glyph = characters[index]
        if glyph.get_editor_property('u_size') == 0:
            # VSize 0 keeps the quad invisible while USize advances the pen.
            glyph.set_editor_property('u_size', advance)
            glyph.set_editor_property('v_size', 0)
            characters[index] = glyph
    font.set_editor_property('characters', characters)


def _remove_redirector(path: str) -> None:
    # Resave every referencer so it points at the redirect target, then delete
    # the redirector. Only the sign Blueprints reference the font; levels use
    # those Blueprints, so no map needs to be loaded here.
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    referencers = registry.get_referencers(
        unreal.Name(path), unreal.AssetRegistryDependencyOptions())
    for referencer in referencers or []:
        package = str(referencer)
        if package == path:
            continue
        asset_data = registry.get_assets_by_package_name(unreal.Name(package))
        if any(str(data.asset_class_path.asset_name) == 'World' for data in asset_data):
            raise RuntimeError(
                f'{package} references {path} directly. Use BP_KPS_Label / '
                'BP_KPS_ExhibitPlate for signs so that levels do not reference the font.')
        unreal.EditorAssetLibrary.load_asset(package)
        unreal.EditorAssetLibrary.save_asset(package, only_if_is_dirty=False)
    if unreal.EditorAssetLibrary.does_asset_exist(path) and \
            not unreal.EditorAssetLibrary.delete_asset(path):
        raise RuntimeError(f'Unable to delete redirector: {path}')


def generate() -> unreal.Font:
    ttf_path = _engine_font_path()
    if not os.path.exists(ttf_path):
        raise RuntimeError(f'Font file not found: {ttf_path}')
    if ctypes.windll.gdi32.AddFontResourceExW(ttf_path, FR_PRIVATE, 0) == 0:
        raise RuntimeError(f'AddFontResourceExW failed: {ttf_path}')

    chars = _read_chars()
    asset_path = f'{FONT_ASSET_DIR}/{FONT_ASSET_NAME}'
    replace = unreal.EditorAssetLibrary.does_asset_exist(asset_path)
    new_name = f'{FONT_ASSET_NAME}_New' if replace else FONT_ASSET_NAME
    try:
        font = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            new_name, FONT_ASSET_DIR, unreal.Font, _make_factory(chars))
    finally:
        ctypes.windll.gdi32.RemoveFontResourceExW(ttf_path, FR_PRIVATE, 0)
    if font is None:
        raise RuntimeError('Font creation failed.')

    _fix_blank_glyphs(font, chars)

    if replace:
        # Redirect references to the new asset instead of deleting the old one,
        # which would clear the Font on every sign that uses it.
        old_font = unreal.load_asset(asset_path)
        if not unreal.EditorAssetLibrary.consolidate_assets(font, [old_font]):
            raise RuntimeError('consolidate_assets failed.')
        _remove_redirector(asset_path)
        new_path = f'{FONT_ASSET_DIR}/{new_name}'
        if not unreal.EditorAssetLibrary.rename_asset(new_path, asset_path):
            raise RuntimeError(f'rename_asset failed: {new_path} -> {asset_path}')
        _remove_redirector(new_path)
        font = unreal.load_asset(asset_path)

    unreal.EditorAssetLibrary.save_loaded_asset(font)
    unreal.log(f'Generated {asset_path}')
    return font


generate()
