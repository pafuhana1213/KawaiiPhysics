"""Rebuild the verification levels from code, inside the editor.

Run through the Python commandlet so no editor window opens:
    UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=Tools/Verification/create_levels.py
        -unattended -nosplash -nopause

The levels hold only what the runner cannot spawn: world geometry and wind sources. Everything under
test comes from the manifest, so the levels rarely change and regenerating them replaces hand edits.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import unreal

FEATURE_MATRIX = '/KawaiiPhysicsDev/Levels/L_KPV_FeatureMatrix'
PERFORMANCE = '/KawaiiPhysicsDev/Levels/L_KPV_Performance'
PERFORMANCE_SUITE = Path(__file__).resolve().parent / 'suites' / 'performance.json'
CUBE = '/Engine/BasicShapes/Cube.Cube'
# The top face sits at Z=0. World collision cells rest their chains against it.
FLOOR_SCALE = unreal.Vector(x=4000.0, y=4000.0, z=1.0)
FLOOR_LOCATION = unreal.Vector(x=0.0, y=0.0, z=-50.0)
# A vertical face at X=-1000 for cells that swing sideways into geometry.
WALL_SCALE = unreal.Vector(x=1.0, y=40.0, z=40.0)
WALL_LOCATION = unreal.Vector(x=-1050.0, y=0.0, z=2000.0)
WIND_ROTATION = unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0)
WIND_STRENGTH = 1.0
SUN_ROTATION = unreal.Rotator(roll=0.0, pitch=-50.0, yaw=30.0)


def spawn(actors, actor_class, location, rotation, label):
    actor = actors.spawn_actor_from_class(actor_class, location, rotation)
    if actor is None:
        raise RuntimeError(f'Could not spawn {label}.')
    actor.set_actor_label(label)
    return actor


def spawn_block(actors, mesh, location, scale, label):
    block = spawn(actors, unreal.StaticMeshActor, location, unreal.Rotator(), label)
    component = block.static_mesh_component
    component.set_static_mesh(mesh)
    block.set_actor_scale3d(scale)
    return block


def create_feature_matrix():
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if unreal.EditorAssetLibrary.does_asset_exist(FEATURE_MATRIX):
        if not unreal.EditorAssetLibrary.delete_asset(FEATURE_MATRIX):
            raise RuntimeError(f'Could not replace {FEATURE_MATRIX}.')
    if not levels.new_level(FEATURE_MATRIX):
        raise RuntimeError(f'Could not create {FEATURE_MATRIX}.')

    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    cube = unreal.EditorAssetLibrary.load_asset(CUBE)
    if cube is None:
        raise RuntimeError(f'Could not load {CUBE}.')
    spawn_block(actors, cube, FLOOR_LOCATION, FLOOR_SCALE, 'Floor')
    spawn_block(actors, cube, WALL_LOCATION, WALL_SCALE, 'Wall')

    wind = spawn(actors, unreal.WindDirectionalSource, unreal.Vector(z=2000.0), WIND_ROTATION, 'Wind')
    wind.get_editor_property('component').set_editor_property('strength', WIND_STRENGTH)
    spawn(actors, unreal.DirectionalLight, unreal.Vector(z=3000.0), SUN_ROTATION, 'Sun')
    spawn(actors, unreal.SkyLight, unreal.Vector(z=3000.0), unreal.Rotator(), 'Sky')

    if not levels.save_current_level():
        raise RuntimeError(f'Could not save {FEATURE_MATRIX}.')
    unreal.log(f'Saved {FEATURE_MATRIX}.')


def create_performance():
    """Create unlit collision geometry at the suite's SimpleWorldCollision cells."""
    suite = json.loads(PERFORMANCE_SUITE.read_text(encoding='utf-8'))
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if unreal.EditorAssetLibrary.does_asset_exist(PERFORMANCE):
        if not unreal.EditorAssetLibrary.delete_asset(PERFORMANCE):
            raise RuntimeError(f'Could not replace {PERFORMANCE}.')
    if not levels.new_level(PERFORMANCE):
        raise RuntimeError(f'Could not create {PERFORMANCE}.')

    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    cube = unreal.EditorAssetLibrary.load_asset(CUBE)
    sphere = unreal.EditorAssetLibrary.load_asset('/Engine/BasicShapes/Sphere.Sphere')
    cylinder = unreal.EditorAssetLibrary.load_asset('/Engine/BasicShapes/Cylinder.Cylinder')
    if not all((cube, sphere, cylinder)):
        raise RuntimeError('Could not load performance geometry meshes.')
    spawn_block(actors, cube, unreal.Vector(x=0, y=0, z=-50),
                unreal.Vector(x=40, y=40, z=1), 'PerformanceFloor')

    grid = suite['grid']
    first = 0
    for group in suite['groups']:
        if group['fixture'] == 'simple_world':
            for local in range(group['count']):
                index = first + local
                x = grid['origin'][0] + index % grid['columns'] * grid['spacing'][0]
                y = grid['origin'][1] + index // grid['columns'] * grid['spacing'][1]
                mesh = (cube, sphere, cylinder)[local % 3]
                spawn_block(actors, mesh, unreal.Vector(x=x + 60, y=y, z=120),
                            unreal.Vector(x=0.5, y=0.5, z=1), f'SimpleWorldShape_{local:02d}')
        first += group['count']
    if not levels.save_current_level():
        raise RuntimeError(f'Could not save {PERFORMANCE}.')
    unreal.log(f'Saved {PERFORMANCE}.')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--only', choices=('feature_matrix', 'performance'))
    arguments, _ = parser.parse_known_args()
    if arguments.only in (None, 'feature_matrix'):
        create_feature_matrix()
    if arguments.only in (None, 'performance'):
        create_performance()


main()
