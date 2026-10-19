using System.Text;

namespace ZhongriSettingsV2;

internal sealed record JapaneseCodeMatch(string Kind, string Code, string Text, string Group, bool Enabled, string? ReservedKey = null)
{
    public string Display => $"{Kind} · {Code} → {Text}" + (Group.Length > 0 ? $" · {Group}" : "") + (Enabled ? "" : " · 未启用");
}

internal sealed partial class JapaneseInputTableStore
{
    internal List<JapaneseCodeMatch> SearchCode(string id, string query, bool virtualSearch = true, bool fuzzySearch = true)
    {
        query = query.Trim();
        if (query.Length > 48) return [];
        lock (_keyCompilationGate)
        {
            var profile = KeyProfile(id);
            var source = File.ReadAllText(Path.Combine(ProfilesDirectory, id + ".tsv"), Encoding.UTF8);
            var compiled = ApplyReservedPhrases(profile, CompileKeyDefinitions(profile, source));
            var rules = Parse(compiled);
            var selection = ReservedSelection(id);
            var results = new List<JapaneseCodeMatch>();
            var catalog = ReservedCatalog(id);
            foreach (var phrase in catalog.Where(p => query.Length == 0 || p.Code.Equals(query, StringComparison.Ordinal) || p.Text.Contains(query, StringComparison.Ordinal)))
                results.Add(new("保留字", phrase.Code, phrase.Text, phrase.Group, IsReservedSelected(selection, phrase), phrase.Key));
            if (query.Length == 0) return results;
            foreach (var rule in rules.Where(p => p.Input == query))
                results.Add(new("表内规则", rule.Input, rule.Output + (rule.Pending.Length > 0 ? $"（待续 {rule.Pending}）" : ""), "", true));
            if (virtualSearch)
            {
                // Mirror production's ordinary-reading route inside longer
                // inputs; a reserved surface is not a phonetic syllable.
                var normal = rules.ToDictionary(r => r.Input, StringComparer.Ordinal);
                foreach (var group in catalog.GroupBy(p => p.Code).Where(g => g.Key != query))
                {
                    var prefix = "# zhongri-normal-reading: " + group.Key + "=";
                    var declaration = compiled.Split('\n').FirstOrDefault(l => l.StartsWith(prefix, StringComparison.Ordinal));
                    if (declaration is not null)
                    {
                        var parts = declaration[prefix.Length..].TrimEnd('\r').Split(';');
                        var pending=parts.ElementAtOrDefault(1) ?? "";
                        // Production represents an unavailable ordinary route
                        // as an unfinished self suffix. It is not a rule that
                        // may recursively reinsert itself in the simulator.
                        if(pending==group.Key)normal.Remove(group.Key);
                        else normal[group.Key] = new(group.Key, parts[0], pending);
                    }
                }
                var parsed = Convert(query, normal.Values.ToList(), IsRgt(compiled));
                results.Add(new("虚拟解析", query, parsed.Text + (parsed.Pending.Length > 0 ? $"（待续 {parsed.Pending}）" : ""), "按当前表试算，不上屏", true));
                var active = catalog.Where(p => selection.Enabled && IsReservedSelected(selection,p)).GroupBy(p=>p.Code).ToDictionary(g=>g.Key,g=>g.Select(p=>p.Text).Distinct().ToList());
                if(!active.ContainsKey(query)) {
                    var parts=new List<List<string>>();var ordinary="";var complete=true;var hasReserved=false;
                    void FlushOrdinary() {
                        if(ordinary.Length==0)return;
                        var reading=Convert(ordinary,normal.Values.ToList(),IsRgt(compiled));
                        if(reading.Pending.Length>0)complete=false;
                        parts.Add([reading.Text]);ordinary="";
                    }
                    for(var offset=0;offset<query.Length;) {
                        var rule=rules.Where(r=>query[offset..].StartsWith(r.Input,StringComparison.Ordinal)).MaxBy(r=>r.Input.Length);
                        var length=rule?.Input.Length??1;
                        if(rule is not null && active.TryGetValue(rule.Input,out var choices)){FlushOrdinary();parts.Add(choices);hasReserved=true;}
                        else ordinary+=query.Substring(offset,length);
                        offset+=length;
                    }
                    FlushOrdinary();
                    if(complete && hasReserved) {
                        var combinations=new List<string>{""};
                        foreach(var part in parts)combinations=combinations.SelectMany(prefix=>part.Select(value=>prefix+value)).Take(16).ToList();
                        foreach(var value in combinations.Distinct())results.Add(new("虚拟保留字组合",query,value,"读音试算，最多 16 分支，不作汉字转换",true));
                    }
                }
            }
            if (fuzzySearch)
            {
                foreach (var phrase in catalog.Where(p => p.Code != query && NearCode(p.Code, query)))
                    results.Add(new("模糊查找", phrase.Code, phrase.Text, phrase.Group, IsReservedSelected(selection, phrase), phrase.Key));
                foreach (var rule in rules.Where(p => p.Input != query && NearCode(p.Input, query)))
                    results.Add(new("模糊查找", rule.Input, rule.Output, "按键表", true));
            }
            return results.Distinct().ToList();
        }
    }

    private static bool NearCode(string a, string b)
    {
        if (Math.Abs(a.Length - b.Length) > 1) return false;
        var i = 0; var j = 0; var edits = 0;
        while (i < a.Length && j < b.Length)
        {
            if (a[i] == b[j]) { i++; j++; continue; }
            if (++edits > 1) return false;
            if (a.Length >= b.Length) i++;
            if (b.Length >= a.Length) j++;
        }
        return edits + a.Length - i + b.Length - j <= 1;
    }
}
