# KawaiiPhysics Toolset の自動登録（UE5.8+ かつ ToolsetRegistry 有効時のみ）
try:
    import toolset_registry  # noqa: F401  # UE5.8+ の ToolsetRegistry プラグインが必要
    from kawaii_physics_toolset import registration
    registration.register_toolsets()
except ImportError:
    pass  # ToolsetRegistry が無い環境では何もしない
except Exception as exc:
    import unreal
    unreal.log_warning(f'KawaiiPhysics Toolset registration failed: {exc}')
