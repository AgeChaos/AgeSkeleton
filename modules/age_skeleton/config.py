def can_build(env, platform):
    env.module_add_dependencies("age_skeleton", ["ecs"])
    return env["target"] == "editor" and not env["disable_3d"]


def configure(env):
    pass
