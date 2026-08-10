from toolset_registry.registration import Registration

from kawaii_physics_toolset.toolset import KawaiiPhysicsToolset


_registration = Registration([
    KawaiiPhysicsToolset,
])


def register_toolsets() -> bool:
    return _registration.register()


def unregister_toolsets() -> None:
    _registration.unregister()
