BEGIN	{
	print "/* file generated from config.table, do not edit! */\n";
	print "#include \"configparam.h\"";
	print "#include \"config_defs.h\"\n";
	print "const struct configparam configparam[] = {";
	FS=",";
	}

	{
	printf "  {\"%s\", %s, %s},\n",$1,$2,$3;
	}

END 	{
	print "};";
	print "";
	print "const int count_configparam = sizeof(configparam) / sizeof(configparam[0]);"
	}
