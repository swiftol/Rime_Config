using System;
using System.IO;
using System.Text;
using System.Text.RegularExpressions;

namespace RimeSettings;

internal static class PagingCompatibility
{
    private static readonly Regex SchemaId = new("(?m)^\\s*schema_id:\\s*['\"]?([^\\s'\"]+)", RegexOptions.Compiled);
    private static readonly Regex PageSize = new(@"(?m)^\s*page_size:\s*(\d+)\s*$", RegexOptions.Compiled);
    private static readonly Regex PatchPageSize = new("(?m)^\\s*['\"]?menu/page_size['\"]?\\s*:\\s*\\d+\\s*$", RegexOptions.Compiled);

    public static void Apply(string rimeDirectory)
    {
        if (!Directory.Exists(rimeDirectory)) return;
        foreach (var schemaPath in Directory.EnumerateFiles(rimeDirectory, "*.schema.yaml", SearchOption.TopDirectoryOnly))
        {
            var schema = File.ReadAllText(schemaPath, Encoding.UTF8);
            var idMatch = SchemaId.Match(schema);
            var sizeMatch = PageSize.Match(schema);
            if (!idMatch.Success || !sizeMatch.Success || !int.TryParse(sizeMatch.Groups[1].Value, out var size) || size >= 100)
                continue;

            var schemaId = idMatch.Groups[1].Value;
            if (schemaId is "rime_ice_japanese_v4" or "rime_ice_japanese_v5") continue;

            var customPath = Path.Combine(rimeDirectory, schemaId + ".custom.yaml");
            var custom = File.Exists(customPath) ? File.ReadAllText(customPath, Encoding.UTF8) : "";
            if (PatchPageSize.IsMatch(custom))
                custom = PatchPageSize.Replace(custom, "  \"menu/page_size\": 100", 1);
            else if (Regex.IsMatch(custom, @"(?m)^patch:\s*$"))
                custom = new Regex(@"(?m)^patch:\s*$").Replace(custom, "patch:\n  \"menu/page_size\": 100", 1);
            else
                custom = custom.TrimEnd() + (custom.Length > 0 ? "\n\n" : "") + "patch:\n  \"menu/page_size\": 100\n";
            File.WriteAllText(customPath, custom.TrimEnd() + "\n", new UTF8Encoding(false));
        }
    }
}
