def can_build(env, platform):
    env.module_add_dependencies("agemesh", ["meshoptimizer"])
    return not env["disable_3d"]


def configure(env):
    pass
