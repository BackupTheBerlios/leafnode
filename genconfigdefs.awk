BEGIN	{
	print "/* file generated from $(srcdir)/config.table, do not edit! */\n";
	print "enum {"
	}

	{
	print $2 ",";
	}

END	{
	print "};"
	}
