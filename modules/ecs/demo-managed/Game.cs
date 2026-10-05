using Godot;
namespace Example;
public sealed class Game : ECSGame
{
    private long moving;
    private long status;
    private double time;
    private int clicks;
    protected override void OnInitialize()
    {
        var models = World.Query("position");
        if (models.Length > 0) moving = models[0];
        status = World.CreateEntity();
        World.SetUI(status, new Godot.Collections.Dictionary {
            ["kind"] = "label", ["rect"] = new Rect2(new Vector2(400, 24), new Vector2(600, 64)),
            ["text"] = "C# game initialized. Click the blue button.", ["font_size"] = 22
        });
        RegisterSystem("example.rotation", delta => {
            time += delta;
            if (World.IsAlive(moving)) World.SetVector(moving, "rotation", new Vector3(0, (float)time, 0));
        });
    }
    protected override void OnUIEvent(long entity, StringName type, Variant value)
    {
        if (type.ToString() != "pressed") return;
        clicks++;
        World.SetUI(status, new Godot.Collections.Dictionary { ["text"] = "C# UI clicks: " + clicks });
    }
    public override void HandleInput(InputEvent input, bool uiHandled)
    {
        if (!uiHandled && input is InputEventKey key && key.Pressed && key.Keycode == Key.Escape) Loop.Quit();
    }
    protected override void OnShutdown()
    {
        GD.Print("ECS_EXAMPLE_STOPPED simulation_seconds=" + time + " clicks=" + clicks);
        if (World.IsAlive(status)) World.DestroyEntity(status);
    }
}
