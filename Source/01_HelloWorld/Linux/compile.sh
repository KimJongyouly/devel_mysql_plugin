# compile 
# -fPIC: Position Independent Code (required for shared libraries).
# -shared: Tells the linker to create a shared object (.so).
# mysql_config: Automatically provides the correct include paths (-I) and library paths (-L) needed for your specific build.



# locate *.so to plugin director 
# /home/mariadb/mariadb_11.4.3/bin/mariadb -u root -p -e "SHOW VARIABLES LIKE 'plugin_dir';"
# Enter password: 
# +---------------+------------------------------------------+
# | Variable_name | Value                                    |
# +---------------+------------------------------------------+
# | plugin_dir    | /home/mariadb/mariadb_11.4.3/lib/plugin/ |
# +---------------+------------------------------------------+

# auth
# sudo cp udf_hello_world.so  /home/mariadb/mariadb_11.4.3/lib/plugin/
# sudo chown mariadb:mariadb /home/mariadb/mariadb_11.4.3/lib/plugin/udf_hello_world.so
# sudo chmod 755   /home/mariadb/mariadb_11.4.3/lib/plugin/udf_hello_world.so


# mariadb plugin등록 
# MariaDB [test]> use test; 
# Database changed
# MariaDB [test]> CREATE FUNCTION hello_world RETURNS STRING SONAME 'udf_hello_world.so';
# Query OK, 0 rows affected (0.037 sec)

# -- Test it
# SELECT hello_world();
# +-----------------------+
# | hello_world()         |
# +-----------------------+
# | Hello World from UDF! |
# +-----------------------+
# 1 row in set (0.000 sec)

# -- If you ever need to remove it
# -- DROP FUNCTION hello_world;
# Query OK, 0 rows affected (0.007 sec)



g++ -fPIC -shared -o udf_hello_world.so udf_hello_world.cpp $(/home/mariadb/mariadb_11.4.3/bin/mysql_config --cflags --libs)
