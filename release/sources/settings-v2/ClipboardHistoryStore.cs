using System.Text.Json;

namespace RimeSettings;

// Keep the same JSON contract as the older WinForms panel without bringing
// System.Windows.Forms and System.Drawing into the WinUI settings process.
internal sealed record ClipboardEntry(
    string Id, string Kind, string Preview, string? Text,
    string? ImagePath, DateTime CreatedAt);

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
        catch { return []; }
    }

    public void AddText(List<ClipboardEntry> entries, string text)
    {
        text = text.Trim();
        if (text.Length == 0) return;
        entries.RemoveAll(item => item.Kind == "text" && item.Text == text);
        var preview = string.Join(" ", text.Split(['\r', '\n'], StringSplitOptions.RemoveEmptyEntries));
        if (preview.Length > 100) preview = preview[..100] + "…";
        entries.Insert(0, new ClipboardEntry(Guid.NewGuid().ToString("N"),
            "text", preview, text, null, DateTime.Now));
        TrimAndSave(entries);
    }

    public void AddImagePng(List<ClipboardEntry> entries, byte[] png, int width, int height)
    {
        if (png.Length < 8 || !png.AsSpan(0, 8).SequenceEqual(
                new byte[] { 137, 80, 78, 71, 13, 10, 26, 10 }))
            throw new InvalidDataException("剪贴板图片不是有效的 PNG 数据。");
        Directory.CreateDirectory(_imageDirectory);
        var id = Guid.NewGuid().ToString("N");
        var path = Path.Combine(_imageDirectory, id + ".png");
        File.WriteAllBytes(path, png);
        entries.Insert(0, new ClipboardEntry(id, "image", $"图片  {width} × {height}",
            null, path, DateTime.Now));
        TrimAndSave(entries);
    }

    public void Delete(List<ClipboardEntry> entries, ClipboardEntry entry)
    {
        entries.RemoveAll(item => item.Id == entry.Id);
        DeleteOwnedImage(entry);
        Save(entries);
    }

    public void Clear(List<ClipboardEntry> entries)
    {
        foreach (var entry in entries) DeleteOwnedImage(entry);
        entries.Clear();
        Save(entries);
    }

    private void TrimAndSave(List<ClipboardEntry> entries)
    {
        while (entries.Count > Limit)
        {
            var last = entries[^1];
            entries.RemoveAt(entries.Count - 1);
            DeleteOwnedImage(last);
        }
        Save(entries);
    }

    private void Save(List<ClipboardEntry> entries)
    {
        Directory.CreateDirectory(_root);
        File.WriteAllText(_historyFile, JsonSerializer.Serialize(entries,
            new JsonSerializerOptions { WriteIndented = true }));
    }

    private void DeleteOwnedImage(ClipboardEntry entry)
    {
        if (entry.Kind != "image" || string.IsNullOrWhiteSpace(entry.ImagePath)) return;
        var ownedRoot = Path.GetFullPath(_imageDirectory) + Path.DirectorySeparatorChar;
        var path = Path.GetFullPath(entry.ImagePath);
        if (!path.StartsWith(ownedRoot, StringComparison.OrdinalIgnoreCase) ||
            !path.EndsWith(".png", StringComparison.OrdinalIgnoreCase)) return;
        if (File.Exists(path)) File.Delete(path);
    }
}
