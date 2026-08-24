using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace RimeSettings;

internal static class PhraseCodeGenerator
{
    public static string Generate(string content, string rimeDirectory)
    {
        var useful = content.EnumerateRunes()
            .Where(r => IsHan(r.Value) || IsAsciiLetterOrDigit(r.Value))
            .Take(3)
            .ToArray();
        if (useful.Length == 0) return "";

        var initials = LoadHanInitials(rimeDirectory);
        var result = new StringBuilder(3);
        foreach (var rune in useful)
        {
            if (IsAsciiLetterOrDigit(rune.Value))
                result.Append(char.ToLowerInvariant((char)rune.Value));
            else if (initials.TryGetValue(rune.ToString(), out var initial))
                result.Append(initial);
        }
        return result.ToString();
    }

    private static Dictionary<string, char> LoadHanInitials(string rimeDirectory)
    {
        var result = new Dictionary<string, char>(StringComparer.Ordinal);
        var candidates = new[]
        {
            Path.Combine(rimeDirectory, "cn_dicts", "8105.dict.yaml"),
            Path.Combine(rimeDirectory, "dicts", "8105.dict.yaml")
        };
        var path = candidates.FirstOrDefault(File.Exists);
        if (path is null) return result;

        foreach (var line in File.ReadLines(path, Encoding.UTF8))
        {
            if (line.Length == 0 || line[0] == '#' || line[0] == '-') continue;
            var parts = line.Split('\t');
            if (parts.Length < 2 || parts[0].EnumerateRunes().Count() != 1) continue;
            var pinyin = parts[1].Trim().ToLowerInvariant();
            var initial = pinyin.FirstOrDefault(c => c is >= 'a' and <= 'z');
            if (initial != default) result.TryAdd(parts[0], initial);
        }
        return result;
    }

    private static bool IsAsciiLetterOrDigit(int value) =>
        value is >= '0' and <= '9' or >= 'A' and <= 'Z' or >= 'a' and <= 'z';

    private static bool IsHan(int value) =>
        value is >= 0x3400 and <= 0x4DBF or >= 0x4E00 and <= 0x9FFF or
        >= 0x20000 and <= 0x2FA1F;
}
