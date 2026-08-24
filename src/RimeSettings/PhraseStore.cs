using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using Microsoft.Win32;

namespace RimeSettings;

internal sealed record CommonPhrase(string Content, string Code, int Weight = 1000000);

internal sealed class PhraseStore
{
    private const string StartMarker = "# --- RimeSettings common phrases begin ---";
    private const string EndMarker = "# --- RimeSettings common phrases end ---";

    public string RimeDirectory { get; }
    public string PhraseFile => Path.Combine(RimeDirectory, "custom_phrase.txt");
    public string PhraseDataFile => Path.Combine(RimeDirectory, "lua", "common_phrase_data.lua");

    public PhraseStore()
    {
        var registered = Registry.CurrentUser.OpenSubKey(@"SOFTWARE\Rime\Weasel")?
            .GetValue("RimeUserDir") as string;
        RimeDirectory = string.IsNullOrWhiteSpace(registered)
            ? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "Rime")
            : Environment.ExpandEnvironmentVariables(registered);
    }

    public List<CommonPhrase> Load()
    {
        if (!File.Exists(PhraseFile)) return [];
        var lines = File.ReadAllLines(PhraseFile, Encoding.UTF8);
        var start = Array.IndexOf(lines, StartMarker);
        var end = Array.IndexOf(lines, EndMarker);
        if (start < 0 || end <= start) return [];

        var result = new List<CommonPhrase>();
        for (var i = start + 1; i < end; i++)
        {
            if (string.IsNullOrWhiteSpace(lines[i]) || lines[i].StartsWith('#')) continue;
            var parts = lines[i].Split('\t');
            if (parts.Length < 2) continue;
            var weight = parts.Length >= 3 && int.TryParse(parts[2], out var parsed) ? parsed : 1000000;
            result.Add(new CommonPhrase(parts[0], parts[1], weight));
        }
        return result;
    }

    public void Save(IEnumerable<CommonPhrase> phrases)
    {
        Directory.CreateDirectory(RimeDirectory);
        var original = File.Exists(PhraseFile)
            ? File.ReadAllText(PhraseFile, Encoding.UTF8)
            : "# Rime table\n# coding: utf-8\n#@/db_name\tcustom_phrase.txt\n#@/db_type\ttabledb\n";

        var start = original.IndexOf(StartMarker, StringComparison.Ordinal);
        var end = original.IndexOf(EndMarker, StringComparison.Ordinal);
        if (start >= 0 && end > start)
        {
            end += EndMarker.Length;
            original = original.Remove(start, end - start).TrimEnd();
        }

        var cleaned = phrases
            .Select(p => new CommonPhrase(Clean(p.Content), CleanCode(p.Code), p.Weight))
            .Where(p => p.Content.Length > 0 && p.Code.Length > 0)
            .ToList();
        var rows = cleaned.Select(p => $"{p.Content}\t{p.Code}\t{p.Weight}");
        var block = StartMarker + "\n" + string.Join("\n", rows) + "\n" + EndMarker;
        File.WriteAllText(PhraseFile, original.TrimEnd() + "\n\n" + block + "\n", new UTF8Encoding(false));
        WriteLuaData(cleaned);
    }

    private void WriteLuaData(IReadOnlyCollection<CommonPhrase> phrases)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(PhraseDataFile)!);
        var groups = phrases
            .GroupBy(p => p.Code, StringComparer.OrdinalIgnoreCase)
            .Select(g => new { Code = g.Key.ToLowerInvariant(), Items = g.Select(p => p.Content).Distinct().ToList() });
        var lines = new List<string> { "return {" };
        foreach (var group in groups)
        {
            var values = string.Join(", ", group.Items.Select(v => $"\"{LuaEscape(v)}\""));
            lines.Add($"  [\"{LuaEscape(group.Code)}\"] = {{ {values} }},");
        }
        lines.Add("}");
        File.WriteAllText(PhraseDataFile, string.Join("\n", lines) + "\n", new UTF8Encoding(false));
    }

    private static string LuaEscape(string value) => value
        .Replace("\\", "\\\\")
        .Replace("\"", "\\\"")
        .Replace("\r", "\\r")
        .Replace("\n", "\\n");

    private static string Clean(string value) =>
        value.Replace('\t', ' ').Replace("\r", " ").Replace("\n", " ").Trim();

    private static string CleanCode(string value) =>
        new(value.Trim().ToLowerInvariant().Where(c => char.IsLetterOrDigit(c) || c is '-' or '_').ToArray());
}
