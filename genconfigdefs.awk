BEGIN	{
	print "/* file generated from $(srcdir)/config.table, do not edit! */\n";
	print "enum {";
	}

	{
	    if (p) {
		print p ",";
		p=""; 
	    }
	    p = $2
	}

END	{
	print p "};"
	}
