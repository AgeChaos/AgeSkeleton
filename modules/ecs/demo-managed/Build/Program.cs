using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text.Json;

if (args.Length < 1) { Console.Error.WriteLine("Usage: GameBuild PROJECT_ROOT [EDITOR_DIRECTORY]"); return 2; }
string root = Path.GetFullPath(args[0]);
var start = new ProcessStartInfo("dotnet") { UseShellExecute = false };
foreach (string argument in new[] { "build", Path.Combine(root, "ECSExample.csproj"), "-c", "Release", "--nologo" }) start.ArgumentList.Add(argument);
using (var build = Process.Start(start) ?? throw new InvalidOperationException("Cannot start dotnet build."))
{
    build.WaitForExit();
    if (build.ExitCode != 0) return build.ExitCode;
}
string managed = Path.Combine(root, "leanclr", "managed");
string bcl = Path.Combine(root, "leanclr", "bcl");
Directory.CreateDirectory(managed);
Directory.CreateDirectory(bcl);
var files = new List<object>();
void Stage(string source, string directory)
{
    string destination = Path.Combine(directory, Path.GetFileName(source));
    File.Copy(source, destination, true);
    string relative = "res://" + Path.GetRelativePath(root, destination).Replace('\\', '/');
    files.Add(new { source = relative, path = relative, sha256 = Convert.ToHexStringLower(SHA256.HashData(File.ReadAllBytes(destination))) });
}
foreach (string file in Directory.EnumerateFiles(Path.Combine(root, "bin", "Release", "net10.0")))
    if (Path.GetExtension(file) is ".dll" or ".pdb") Stage(file, managed);
foreach (string file in Directory.EnumerateFiles(RuntimeEnvironment.GetRuntimeDirectory(), "*.dll")) Stage(file, bcl);
string manifest = Path.Combine(root, "leanclr", "export-files.json");
File.WriteAllText(manifest + ".tmp", JsonSerializer.Serialize(new { version = 1, files }, new JsonSerializerOptions { WriteIndented = true }));
File.Move(manifest + ".tmp", manifest, true);
Console.WriteLine("ECS C# build and export manifest ready.");
return 0;
