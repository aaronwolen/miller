mlr --opprint --from data/small head -n 2 then put -q '
  begin {
    @myvar["nesting-is-too-shallow"] = 1;
    @myvar["nesting-is"]["just-right"] = 2;
    @myvar["nesting-is"]["also-just-right"] = 3;
    @myvar["nesting"]["is"]["too-deep"] = 4;
  }
  end {
    dump
  }
'
