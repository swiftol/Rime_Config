using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Automation;
using System.Globalization;

namespace ZhongriSettingsV2;

public sealed partial class MainWindow
{
    private TextBox? _candidateSize;
    private TextBox? _candidateMinHeight;
    private Canvas? _appearanceCanvas;
    private ScrollViewer? _appearancePreviewScroll;
    private Border? _appearancePanel;
    private Grid? _appearanceGrid;
    private TextBlock? _appearanceSelection;
    private TextBlock? _appearancePreedit;
    private string _appearanceColorKey="selectedText";
    private readonly List<(Border Box,TextBlock Word,TextBlock Comment,string TextKey,string BackKey,bool Selected)> _appearanceExamples=[];
    private Thumb? _appearanceWidthHandle;
    private Thumb? _appearanceHeightHandle;
    private double _appearanceX=28,_appearanceY=28;
    private readonly Dictionary<string,double> _appearanceDragResidual=[];

    private static SolidColorBrush DesignerBrush(System.Drawing.Color color,System.Drawing.Color fallback) =>
        new(Windows.UI.Color.FromArgb(255,color.A==0?fallback.R:color.R,color.A==0?fallback.G:color.G,color.A==0?fallback.B:color.B));
    private static Thumb DesignerThumb() => new() {
        Template=(ControlTemplate)Microsoft.UI.Xaml.Markup.XamlReader.Load("<ControlTemplate xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' xmlns:p='using:Microsoft.UI.Xaml.Controls.Primitives' TargetType='p:Thumb'><Border Background='{TemplateBinding Background}' CornerRadius='3'/></ControlTemplate>")
    };

    private FrameworkElement BuildAppearanceDesigner()
    {
        AppearanceTestStep("build designer");
        _appearanceExamples.Clear();
        var body=new StackPanel { Spacing=8 };
        _appearanceSelection=new TextBlock { Text="已选：选中项文字 · 点击示例中的文字或背景选择颜色",TextWrapping=TextWrapping.Wrap };
        body.Children.Add(_appearanceSelection);
        var controls=new StackPanel { Orientation=Orientation.Horizontal,Spacing=8 };
        var color=new Button { Content="调节选中部位的颜色" };
        color.Click+=(_,_)=> {
            if(_colorChooseButtons.TryGetValue(_appearanceColorKey,out var choose)) choose.Flyout?.ShowAt(color);
        };
        controls.Children.Add(color);
        var resetPosition=new Button { Content="预览回到左上角" };
        resetPosition.Click+=(_,_)=> { _appearanceX=28;_appearanceY=28;RefreshAppearanceDesigner(); };
        controls.Children.Add(resetPosition);
        var automaticHeight=new Button { Content="恢复自动高度" };
        automaticHeight.Click+=(_,_)=> { if(_candidateMinHeight is not null) _candidateMinHeight.Text="0"; };
        controls.Children.Add(automaticHeight);
        body.Children.Add(controls);

        _appearanceCanvas=new Canvas { Width=1450,Height=200,Background=Brush(28,29,33) };
        AutomationProperties.SetAutomationId(_appearanceCanvas,"AppearanceDesignerCanvas");
        _appearancePanel=new Border { CornerRadius=new CornerRadius(10),BorderThickness=new Thickness(2),BorderBrush=Brush(95,100,112),Padding=new Thickness(10) };
        AutomationProperties.SetAutomationId(_appearancePanel,"AppearanceDesignerPanel");
        _appearancePanel.BorderThickness=new Thickness(0);
        _appearancePanel.Padding=new Thickness(8);
        var panelContent=new StackPanel { Spacing=8 };
        var title=new Grid();
        _appearancePreedit=new TextBlock { Text="ka  ˎ",FontFamily=new FontFamily("Microsoft YaHei"),Foreground=Brush(19,190,202),IsHitTestVisible=false };
        title.Children.Add(_appearancePreedit);
        var move=DesignerThumb(); move.Background=new SolidColorBrush(Windows.UI.Color.FromArgb(1,0,0,0));move.HorizontalAlignment=HorizontalAlignment.Stretch;move.VerticalAlignment=VerticalAlignment.Stretch;
        AutomationProperties.SetAutomationId(move,"AppearanceMoveHandle");
        ToolTipService.SetToolTip(move,"拖动移动预览；真实候选窗仍跟随输入光标");
        move.DragDelta+=(_,e)=> {
            _appearanceX=Math.Clamp(_appearanceX+e.HorizontalChange,0,Math.Max(0,1450-_appearancePanel.Width-24));
            _appearanceY=Math.Clamp(_appearanceY+e.VerticalChange,0,90); RefreshAppearanceDesigner();
        };
        title.Children.Add(move); panelContent.Children.Add(title);
        AppearanceTestStep("move handle built");
        _appearanceGrid=new Grid { ColumnSpacing=0 };
        for(int i=0;i<9;i++) _appearanceGrid.ColumnDefinitions.Add(new ColumnDefinition { Width=GridLength.Auto });
        _appearanceGrid.RowDefinitions.Add(new RowDefinition { Height=GridLength.Auto });
        var samples=new[] {
            ("か","☁\n[か]","japaneseText","selectedBackground",true),
            ("化","☁\n[か]","japaneseText","japaneseBackground",false),
            ("卡","card\nカード","chineseText","chineseBackground",false),
            ("咖","Coffee\nコーヒー","chineseText","chineseBackground",false),
            ("喀","Ka\nカッ","chineseText","chineseBackground",false),
            ("下","Below\n[か]","japaneseText","japaneseBackground",false),
            ("咔","click\nカチッ","chineseText","chineseBackground",false),
            ("過","Pass\n[か]","japaneseText","japaneseBackground",false),
            ("家","Home\n[か]","japaneseText","japaneseBackground",false)
        };
        for(int i=0;i<samples.Length;i++) {
            var sample=samples[i];
            var stack=new StackPanel { Spacing=2 };
            var word=new TextBlock { Text=sample.Item1,FontFamily=new FontFamily("Microsoft YaHei"),TextWrapping=TextWrapping.NoWrap };
            var comment=new TextBlock { Text=sample.Item2,FontFamily=new FontFamily("Microsoft YaHei"),TextWrapping=TextWrapping.NoWrap,TextTrimming=TextTrimming.CharacterEllipsis };
            ToolTipService.SetToolTip(comment,sample.Item2);
            var box=new Border { Child=stack,CornerRadius=new CornerRadius(0),VerticalAlignment=VerticalAlignment.Stretch };
            word.Tapped+=(_,e)=> { SelectAppearanceColor(sample.Item3,"候选文字"); e.Handled=true; };
            comment.Tapped+=(_,e)=> { SelectAppearanceColor("annotationText","翻译注释"); e.Handled=true; };
            box.Tapped+=(_,e)=> { SelectAppearanceColor(sample.Item4,"候选背景");e.Handled=true; };
            var wordRegion=new Grid { ColumnSpacing=4 };
            wordRegion.ColumnDefinitions.Add(new ColumnDefinition { Width=GridLength.Auto });
            wordRegion.ColumnDefinitions.Add(new ColumnDefinition { Width=GridLength.Auto });
            var label=new TextBlock { Text=(i+1).ToString(),FontFamily=new FontFamily("Microsoft YaHei"),FontSize=_store.ReadAppearanceLabelSize()*4.0/3,Foreground=Brush(240,240,240) };
            wordRegion.Children.Add(label); Grid.SetColumn(word,1); wordRegion.Children.Add(word);
            var wordGrip=DesignerThumb();wordGrip.Width=8;wordGrip.Height=8;wordGrip.Opacity=.25;wordGrip.Background=Brush(40,148,170);wordGrip.HorizontalAlignment=HorizontalAlignment.Right;wordGrip.VerticalAlignment=VerticalAlignment.Bottom;
            ToolTipService.SetToolTip(wordGrip,"拖文字右下角调字号，向下／右放大；所有候选共用，真实输入框即时更新");
            wordGrip.DragDelta+=(_,e)=>AdjustDesignerValue("candidate",(Math.Abs(e.HorizontalChange)>Math.Abs(e.VerticalChange)?e.HorizontalChange:e.VerticalChange)/3);
            AutomationProperties.SetAutomationId(wordGrip,"AppearanceWordHandle_"+i);
            Grid.SetColumn(wordGrip,1); wordRegion.Children.Add(wordGrip);
            var commentRegion=new Grid();commentRegion.Children.Add(comment);
            var commentGrip=DesignerThumb();commentGrip.Width=8;commentGrip.Height=8;commentGrip.Opacity=.25;commentGrip.Background=Brush(82,123,140);commentGrip.HorizontalAlignment=HorizontalAlignment.Right;commentGrip.VerticalAlignment=VerticalAlignment.Bottom;
            ToolTipService.SetToolTip(commentGrip,"拖注释右下角调字号；真实输入框即时更新");
            commentGrip.DragDelta+=(_,e)=>AdjustDesignerValue("comment",(Math.Abs(e.HorizontalChange)>Math.Abs(e.VerticalChange)?e.HorizontalChange:e.VerticalChange)/3);
            AutomationProperties.SetAutomationId(commentGrip,"AppearanceCommentHandle_"+i);
            commentRegion.Children.Add(commentGrip);
            stack.Children.Add(wordRegion);stack.Children.Add(commentRegion);
            box.PointerEntered+=(_,_)=> { wordGrip.Opacity=commentGrip.Opacity=1; };
            box.PointerExited+=(_,_)=> { wordGrip.Opacity=commentGrip.Opacity=.25; };
            Grid.SetColumn(box,i); _appearanceGrid.Children.Add(box);
            _appearanceExamples.Add((box,word,comment,sample.Item3,sample.Item4,sample.Item5));
            var gapGrip=DesignerThumb(); gapGrip.Width=5;gapGrip.Background=new SolidColorBrush(Windows.UI.Color.FromArgb(35,36,182,201));
            gapGrip.HorizontalAlignment=HorizontalAlignment.Right; gapGrip.Margin=new Thickness(0,0,-8,0);
            ToolTipService.SetToolTip(gapGrip,"左右拖候选间隔，调整真实候选间距");
            gapGrip.DragDelta+=(_,e)=>AdjustDesignerValue("spacing",e.HorizontalChange);
            Grid.SetColumn(gapGrip,i); _appearanceGrid.Children.Add(gapGrip);
            if(i==0) {
                var paddingGrip=DesignerThumb(); paddingGrip.Width=4; paddingGrip.HorizontalAlignment=HorizontalAlignment.Left; paddingGrip.Background=new SolidColorBrush(Windows.UI.Color.FromArgb(30,255,255,255));
                ToolTipService.SetToolTip(paddingGrip,"拖选中背景左边界调留白，向左扩大");
                paddingGrip.DragDelta+=(_,e)=>AdjustDesignerValue("padding",-e.HorizontalChange);
                Grid.SetColumn(paddingGrip,i); _appearanceGrid.Children.Add(paddingGrip);
            }
        }
        panelContent.Children.Add(_appearanceGrid); _appearancePanel.Child=panelContent;
        AppearanceTestStep("examples built");
        _appearancePanel.Tapped+=(_,_)=>SelectAppearanceColor("panelBackground","候选框背景");
        _appearanceCanvas.Children.Add(_appearancePanel);
        _appearanceWidthHandle=DesignerThumb();_appearanceWidthHandle.Width=7;_appearanceWidthHandle.Height=95;_appearanceWidthHandle.Opacity=.35;_appearanceWidthHandle.Background=Brush(36,182,201);
        AutomationProperties.SetAutomationId(_appearanceWidthHandle,"AppearanceWidthHandle");
        ToolTipService.SetToolTip(_appearanceWidthHandle,"左右拖动：真实候选框最小宽度；实际宽度仍随候选内容伸缩");
        _appearanceWidthHandle.DragDelta+=(_,e)=>AdjustDesignerValue("width",e.HorizontalChange);
        _appearanceCanvas.Children.Add(_appearanceWidthHandle);
        _appearanceHeightHandle=DesignerThumb();_appearanceHeightHandle.Width=140;_appearanceHeightHandle.Height=7;_appearanceHeightHandle.Opacity=.35;_appearanceHeightHandle.Background=Brush(36,182,201);
        AutomationProperties.SetAutomationId(_appearanceHeightHandle,"AppearanceHeightHandle");
        ToolTipService.SetToolTip(_appearanceHeightHandle,"上下拖动：候选框最小高度（不会裁掉内容）");
        _appearanceHeightHandle.DragDelta+=(_,e)=>AdjustDesignerValue("height",e.VerticalChange);
        _appearanceCanvas.Children.Add(_appearanceHeightHandle);
        foreach(var edge in new[]{_appearanceWidthHandle,_appearanceHeightHandle}) {
            edge.PointerEntered+=(_,_)=>edge.Opacity=1;edge.PointerExited+=(_,_)=>edge.Opacity=.35;
        }
        _appearancePanel.SizeChanged+=(_,e)=>PositionAppearanceHeightHandle();
        _appearancePreviewScroll=new ScrollViewer { Content=_appearanceCanvas,
            MaxHeight=Math.Clamp(Root.ActualHeight*.27,160,200),
            HorizontalScrollBarVisibility=ScrollBarVisibility.Auto,HorizontalScrollMode=ScrollMode.Enabled,
            VerticalScrollBarVisibility=ScrollBarVisibility.Auto,VerticalScrollMode=ScrollMode.Enabled };
        // Reserve space for the settings below even on a short window. Large
        // fonts/heights remain reachable within the preview's own viewport.
        body.Children.Add(_appearancePreviewScroll);
        void FitPreview() {
            if(_appearancePreviewScroll is not null)
                _appearancePreviewScroll.MaxHeight=Math.Clamp(Root.ActualHeight*.27,160,200);
            if(_appearanceSelection is not null)
                _appearanceSelection.Visibility=Root.ActualHeight<650?Visibility.Collapsed:Visibility.Visible;
        }
        body.SizeChanged+=(_,_)=>FitPreview();
        FitPreview();

        foreach(var field in new[] {_width,_spacing,_padding,_commentSize,_candidateSize,_candidateMinHeight})
            if(field is not null) field.TextChanged+=(_,_)=>RefreshAppearanceDesigner();
        RefreshAppearanceDesigner();
        AppearanceTestStep("designer refreshed");
        var card=Card("","",body);
        card.Padding=new Thickness(12);
        ToolTipService.SetToolTip(card,"拖文字／注释右下角改字号，拖候选分隔线改间距，拖选中底色左边界改留白，拖外框边缘改大小。自动保存并作用于真实输入框。");
        return card;
    }
    private static void AppearanceTestStep(string step) {
        if(Environment.GetCommandLineArgs().Contains("--appearance-designer-self-test"))
            File.AppendAllText(Path.Combine(Path.GetTempPath(),"ZhongriSettingsV2-appearance-designer-test.txt"),"\nSTEP "+step);
    }

    private void SelectAppearanceColor(string key,string label) {
        _appearanceColorKey=key;
        var title=key switch { "reservedText"=>"保留字片段","chineseText"=>"中文文字","chineseBackground"=>"中文底色","japaneseText"=>"日语文字","japaneseBackground"=>"日语底色","phraseText"=>"常用语文字","phraseBackground"=>"常用语底色","fuzzyCorrectionText"=>"模糊纠错字","selectedText"=>"选中项文字","selectedBackground"=>"选中项背景","annotationText"=>"翻译注释",_=>"候选框背景" };
        if(_appearanceSelection is not null) _appearanceSelection.Text="已选："+title+"（同类区域共用）；点击调色按钮修改";
    }
    private void AdjustDesignerValue(string role,double delta) {
        var (field,min,max)=role switch {
            "width"=>(_width,520,1200),"spacing"=>(_spacing,4,36),"padding"=>(_padding,2,18),
            "candidate"=>(_candidateSize,12,40),"height"=>(_candidateMinHeight,0,500),_=>(_commentSize,8,18)
        };
        if(field is null) return;
        var accumulated=_appearanceDragResidual.GetValueOrDefault(role)+delta;
        var steps=(int)Math.Truncate(accumulated+Math.Sign(accumulated)*1e-9);
        _appearanceDragResidual[role]=accumulated-steps;
        if(steps==0) return;
        var current=int.TryParse(field.Text,out var number)?number:min;
        if(role=="height" && _appearancePanel is not null) current=Math.Max(current,(int)Math.Round(_appearancePanel.ActualHeight));
        field.Text=Math.Clamp(current+steps,min,max).ToString(CultureInfo.InvariantCulture);
        _appearanceChanged?.Invoke();
    }
    private void RefreshAppearanceDesigner() {
        if(_appearancePanel is null || _appearanceGrid is null || _appearanceCanvas is null || !_colors.ContainsKey("panelBackground")) return;
        int Value(TextBox? field,int fallback)=>int.TryParse(field?.Text,out var number)?number:fallback;
        var width=Math.Clamp(Value(_width,600),520,1200);
        var gap=Math.Clamp(Value(_spacing,16),4,36);
        var padding=Math.Clamp(Value(_padding,8),2,18);
        _appearancePanel.MinHeight=Math.Clamp(Value(_candidateMinHeight,0),0,500);
        _appearancePanel.Background=DesignerBrush(_colors["panelBackground"],System.Drawing.Color.FromArgb(255,64,64,64));
        Canvas.SetLeft(_appearancePanel,_appearanceX);Canvas.SetTop(_appearancePanel,_appearanceY);
        _appearanceGrid.ColumnSpacing=Math.Max(gap,2*padding);
        foreach(var child in _appearanceGrid.Children) child.Visibility=Visibility.Visible;
        foreach(var example in _appearanceExamples) {
            example.Box.Padding=new Thickness(example.Selected?padding:0);
            example.Box.Margin=new Thickness(example.Selected?-padding:0);
            example.Box.Background=DesignerBrush(_colors[example.BackKey],example.Selected?System.Drawing.Color.FromArgb(255,20,184,196):_colors["panelBackground"]);
            example.Word.Foreground=DesignerBrush(_colors[example.TextKey],System.Drawing.Color.White);
            example.Word.FontSize=Math.Clamp(Value(_candidateSize,14),12,40)*4.0/3;
            example.Comment.Foreground=DesignerBrush(_colors["annotationText"],System.Drawing.Color.LightGray);
            example.Comment.FontSize=Math.Clamp(Value(_commentSize,11),8,18)*4.0/3;
            example.Comment.MaxWidth=100; // Native compact annotations cap at 600 / 6.
        }
        if(_appearancePreedit is not null) _appearancePreedit.FontSize=Math.Clamp(Value(_candidateSize,14),12,40)*4.0/3;
        _appearanceGrid.Measure(new Windows.Foundation.Size(double.PositiveInfinity,double.PositiveInfinity));
        double used=16;
        int visible=0;
        foreach(var example in _appearanceExamples) {
            var next=used+(visible==0?0:_appearanceGrid.ColumnSpacing)+example.Box.DesiredSize.Width;
            if(visible>0 && next>600) break;
            used=next;visible++;
        }
        foreach(var child in _appearanceGrid.Children)
            child.Visibility=Grid.GetColumn((FrameworkElement)child)<visible?Visibility.Visible:Visibility.Collapsed;
        _appearancePanel.Width=Math.Max(width,used);
        PositionAppearanceHeightHandle();
    }
    private void PositionAppearanceHeightHandle() {
        if(_appearancePanel is null || _appearanceHeightHandle is null || _appearanceCanvas is null) return;
        _appearanceHeightHandle.Width=Math.Max(30,_appearancePanel.Width-16);
        Canvas.SetLeft(_appearanceHeightHandle,_appearanceX+8);
        Canvas.SetTop(_appearanceHeightHandle,_appearanceY+_appearancePanel.ActualHeight);
        if(_appearanceWidthHandle is not null) {
            _appearanceWidthHandle.Height=Math.Max(30,_appearancePanel.ActualHeight-16);
            Canvas.SetLeft(_appearanceWidthHandle,_appearanceX+_appearancePanel.Width);
            Canvas.SetTop(_appearanceWidthHandle,_appearanceY+8);
        }
        _appearanceCanvas.Height=Math.Max(200,_appearanceY+_appearancePanel.ActualHeight+32);
    }

    internal async Task RunAppearanceDesignerSelfTestAsync(string imagePath) {
        void Step(string step)=>File.AppendAllText(Path.Combine(Path.GetTempPath(),"ZhongriSettingsV2-appearance-designer-test.txt"),"\nSTEP "+step);
        Step("select appearance");
        SelectPage("appearance"); Root.UpdateLayout(); await Task.Delay(200);
        // Fixed preview and mutually exclusive settings panes, including after
        // navigating away/back. This uses only the isolated self-test store.
        var before=_appearancePinned.TransformToVisual(Root).TransformPoint(new Windows.Foundation.Point());
        foreach(var showColors in new[]{false,true,false}) {
            _selectAppearanceSection!(showColors);Root.UpdateLayout();await Task.Delay(100);
            var tabs=((Grid)_appearancePinned.Children.Last()).Children.OfType<ToggleButton>().ToArray();
            if(tabs.Length!=2 || tabs[0].IsChecked!=!showColors || tabs[1].IsChecked!=showColors ||
               PageContent.Children[showColors?1:0].Visibility!=Visibility.Visible ||
               PageContent.Children[showColors?0:1].Visibility!=Visibility.Collapsed)
                throw new InvalidOperationException("Appearance tabs are not mutually exclusive");
            _contentScroll!.ChangeView(null,Math.Min(120,_contentScroll.ScrollableHeight/2),null,true);
            await Task.Delay(150);Root.UpdateLayout();
            var after=_appearancePinned.TransformToVisual(Root).TransformPoint(new Windows.Foundation.Point());
            await RenderAppearanceElementAsync(Root,Path.Combine(Path.GetDirectoryName(imagePath)!,showColors?"appearance-colors-scrolled.png":"appearance-dimensions-scrolled.png"));
            if(_contentScroll.VerticalOffset<=0 || Math.Abs(before.Y-after.Y)>.1 ||
               _appearancePinned.ActualHeight>=Root.ActualHeight-80)
                throw new InvalidOperationException($"Preview moved or settings viewport is unusable: colors={showColors} offset={_contentScroll.VerticalOffset} extent={_contentScroll.ScrollableHeight} viewport={_contentScroll.ViewportHeight} pin={_appearancePinned.ActualHeight} root={Root.ActualHeight} y={before.Y}/{after.Y}");
        }
        _contentScroll!.ChangeView(null,0,null,true);
        Step("fixed preview and both section scroll checks passed");
        Step("drag checks");
        if(_appearanceExamples.Count!=9 || _appearancePanel is null || _appearanceCanvas is null || _appearanceWidthHandle is null)
            throw new InvalidOperationException("Missing graphical editor examples/handles");
        if(!IsInteractivePointerTarget(_appearanceWidthHandle)) throw new InvalidOperationException("Dragging handle treated as blank background");
        _width!.Text="600";_spacing!.Text="16";_padding!.Text="8";_candidateSize!.Text="24";_commentSize!.Text="11";
        AdjustDesignerValue("width",80); AdjustDesignerValue("spacing",5); AdjustDesignerValue("padding",2);
        AdjustDesignerValue("candidate",4); AdjustDesignerValue("comment",2);
        for(int i=0;i<10;i++) AdjustDesignerValue("width",0.1);
        if(_width.Text!="681") throw new InvalidOperationException("Width drag accumulation failed");
        SelectAppearanceColor("japaneseText","日语文字");
        _colorPickers["japaneseText"].Color=Windows.UI.Color.FromArgb(255,255,221,64);
        if(_appearanceColorKey!="japaneseText" || _colors["japaneseText"].R!=255) throw new InvalidOperationException("Preview palette did not synchronize");
        _appearanceX=44;_appearanceY=40;RefreshAppearanceDesigner(); Root.UpdateLayout();
        AdjustDesignerValue("height",24);Root.UpdateLayout();
        if(Canvas.GetLeft(_appearancePanel)!=44 || _appearancePanel.Width<680) throw new InvalidOperationException("Preview position/size failed");
        await Task.Delay(650);
        Step("save finished");
        var saved=_store.ReadAppearance();
        if(saved.CandidateSize!=28 || saved.CommentSize!=13 || saved.CandidateSpacing!=21 || saved.HighlightPadding!=10 || saved.JapaneseText.R!=255 || saved.MinHeight<=0)
            throw new InvalidOperationException("Graphical controls did not save actual appearance options");
        using(var hot=Microsoft.Win32.Registry.CurrentUser.OpenSubKey(_store.HotSettingsRegistryPath))
            if(hot?.GetValue("CandidateFontSize") is not int font || font!=28 || _saveStatus!.Text.StartsWith("未保存"))
                throw new InvalidOperationException("Drag did not publish actual backend hot settings: "+_saveStatus?.Text);
        SelectPage("appearance"); Root.UpdateLayout();
        if(_candidateSize!.Text!="28" || _spacing!.Text!="21") throw new InvalidOperationException("Reopen lost edited dimensions");
        // Render the user's compact ka example at its real 12-point scale,
        // after independently checking that larger dragged values persisted.
        _candidateSize.Text="12";_commentSize!.Text="11";_spacing.Text="16";_padding!.Text="8";_width!.Text="520";_candidateMinHeight!.Text="0";
        RefreshAppearanceDesigner();Root.UpdateLayout();await Task.Delay(650);
        if(_appearanceGrid!.RowDefinitions.Count!=1 || _appearanceGrid.ColumnDefinitions.Any(c=>c.Width.GridUnitType!=GridUnitType.Auto) ||
            _appearanceExamples.Any(e=>e.Word.TextWrapping!=TextWrapping.NoWrap) || _appearanceExamples.Select(e=>Math.Round(e.Box.ActualWidth)).Distinct().Count()<3)
            throw new InvalidOperationException("Preview is not a natural-width, one-row real candidate layout");
        await RenderAppearanceCanvasAsync(imagePath);
    }

    internal async Task CaptureLiveAppearancePreviewAsync(string imagePath) {
        SelectPage("appearance");Root.UpdateLayout();await Task.Delay(300);
        var value=CaptureAppearance();
        File.WriteAllText(Path.ChangeExtension(imagePath,"json"),System.Text.Json.JsonSerializer.Serialize(new {
            value.Width,value.CandidateSize,value.CommentSize,value.CandidateSpacing,value.HighlightPadding,
            Panel=value.PanelBackground?.ToArgb(),Comment=value.AnnotationText?.ToArgb(),Selected=value.SelectedBackground?.ToArgb()
        }));
        await RenderAppearanceCanvasAsync(imagePath);
    }

    private async Task RenderAppearanceCanvasAsync(string imagePath) {
        await RenderAppearanceElementAsync(_appearanceCanvas!,imagePath);
    }

    private async Task RenderAppearanceElementAsync(FrameworkElement element,string imagePath) {
        var bitmap=new Microsoft.UI.Xaml.Media.Imaging.RenderTargetBitmap();
        await bitmap.RenderAsync(element);
        var pixels=await bitmap.GetPixelsAsync();
        var folder=await Windows.Storage.StorageFolder.GetFolderFromPathAsync(Path.GetDirectoryName(imagePath)!);
        var file=await folder.CreateFileAsync(Path.GetFileName(imagePath),Windows.Storage.CreationCollisionOption.ReplaceExisting);
        using var stream=await file.OpenAsync(Windows.Storage.FileAccessMode.ReadWrite);
        var encoder=await Windows.Graphics.Imaging.BitmapEncoder.CreateAsync(Windows.Graphics.Imaging.BitmapEncoder.PngEncoderId,stream);
        using var reader=Windows.Storage.Streams.DataReader.FromBuffer(pixels);
        var data=new byte[pixels.Length];reader.ReadBytes(data);
        encoder.SetPixelData(Windows.Graphics.Imaging.BitmapPixelFormat.Bgra8,Windows.Graphics.Imaging.BitmapAlphaMode.Premultiplied,(uint)bitmap.PixelWidth,(uint)bitmap.PixelHeight,96,96,data);
        await encoder.FlushAsync();
    }
}
