-- Opt-in, bounded diagnostics for the three reported public reproduction codes.
return function(stage,code,candidates)
  if os.getenv('ZHONGRI_JA_DIAGNOSTIC')~='1' or
     not ({mouowarika=true,manuke=true,dousiyoumonai=true,kaeliTq=true,kolehaTakq=true,simeTekudasq=true})[code] then return end
  local file=io.open('H:/Temp/ZhongriRegression/candidate-diagnostic.log','a')
  if not file then return end
  file:write(stage,' ',code,'\n')
  for index=1,math.min(8,#candidates) do
    local c=candidates[index]
    file:write(tostring(index),' ',c.type or '', ' ',c.text or '', ' ',c.comment or '', '\n')
  end
  file:close()
end
