using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Linq;
using System.Text.Json;

namespace RimeSettings;

internal sealed record ClipboardEntry(
    string Id,
    string Kind,
    string Preview,
    string? Text,
    string? ImagePath,
    DateTime CreatedAt);

internal sealed class ClipboardHistoryStore
{
    private const int Limit = 50;
    private readonly string _root;
    private readonly string _historyFile;
    private readonly string _imageDirectory;

    public ClipboardHistoryStore(string rimeDirectory)
    {
        _root = Path.Combine(rimeDirectory, "clipboard");
        _historyFile = Path.Combine(_root, "history.json");
        _imageDirectory = Path.Combine(_root, "images");
    }

    public List<ClipboardEntry> Load()
    {
        try
        {
            if (!File.Exists(_historyFile)) return [];
            return JsonSerializer.Deserialize<List<ClipboardEntry>>(File.ReadAllText(_historyFile)) ?? [];
        }
        catch
        {
            return [];
        }
    }

    public void AddText(List<ClipboardEntry> entries, string text)
    {
        text = text.Trim();
        if (text.Length == 0) return;
        entries.RemoveAll(item => item.Kind == "text" && item.Text == text);
        entries.Insert(0, new ClipboardEntry(
            Guid.NewGuid().ToString("N"), "text", MakePreview(text), text, null, DateTime.Now));
        TrimAndSave(entries);
    }

    public void AddImage(List<ClipboardEntry> entries, Image image)
    {
        Directory.CreateDirectory(_imageDirectory);
        var id = Guid.NewGuid().ToString("N");
        var path = Path.Combine(_imageDirectory, id + ".png");
        image.Save(path, ImageFormat.Png);
        entries.Insert(0, new ClipboardEntry(
            id, "image", $"图片  {image.Width} × {image.Height}", null, path, DateTime.Now));
        TrimAndSave(entries);
    }

    public void Delete(List<ClipboardEntry> entries, ClipboardEntry entry)
    {
        entries.RemoveAll(item => item.Id == entry.Id);
        DeleteImage(entry);
        Save(entries);
    }

    public void Clear(List<ClipboardEntry> entries)
    {
        foreach (var entry in entries) DeleteImage(entry);
        entries.Clear();
        Save(entries);
    }

    public void Save(List<ClipboardEntry> entries)
    {
        Directory.CreateDirectory(_root);
        File.WriteAllText(_historyFile, JsonSerializer.Serialize(entries, new JsonSerializerOptions { WriteIndented = true }));
    }

    private void TrimAndSave(List<ClipboardEntry> entries)
    {
        while (entries.Count > Limit)
        {
            var last = entries[^1];
            entries.RemoveAt(entries.Count - 1);
            DeleteImage(last);
        }
        Save(entries);
    }

    private static string MakePreview(string text)
    {
        var singleLine = string.Join(" ", text.Split(new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries));
        return singleLine.Length > 100 ? singleLine[..100] + "…" : singleLine;
    }

    private static void DeleteImage(ClipboardEntry entry)
    {
        try
        {
            if (!string.IsNullOrWhiteSpace(entry.ImagePath) && File.Exists(entry.ImagePath))
                File.Delete(entry.ImagePath);
        }
        catch { }
    }
}
