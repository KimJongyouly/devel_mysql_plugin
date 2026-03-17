# Get compiler flags from mysql_config
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
# sudo cp udf_hello_world_args.so  /home/mariadb/mariadb_11.4.3/lib/plugin/
# sudo chown mariadb:mariadb /home/mariadb/mariadb_11.4.3/lib/plugin/udf_hello_world_args.so
# sudo chmod 755   /home/mariadb/mariadb_11.4.3/lib/plugin/udf_hello_world_args.so



# mariadb plugin등록 
# MariaDB [test]> use test; 
# Database changed
# MariaDB [test]> CREATE FUNCTION hello_world RETURNS STRING SONAME 'udf_hello_world_args.so';
# Query OK, 0 rows affected (0.037 sec)

# MariaDB [test]> select hello_world('youly'); 
# +----------------------+
# | hello_world('youly') |
# +----------------------+
# | Hello World, youly!  |
# +----------------------+
# 1 row in set (0.000 sec)

# MariaDB [test]> create table test1 (a varchar(10)); 
# Query OK, 0 rows affected (0.010 sec)
# MariaDB [test]> insert into test1(a) values('a1'),('a2'),('a3'); 
# Query OK, 3 rows affected (0.007 sec)
# Records: 3  Duplicates: 0  Warnings: 0

# MariaDB [test]> select * from test1; 
# +------+
# | a    |
# +------+
# | a1   |
# | a2   |
# | a3   |
# +------+
# 3 rows in set (0.001 sec)

# MariaDB [test]> select hello_world(a) from test1; 
# +------------------+
# | hello_world(a)   |
# +------------------+
# | Hello World, a1! |
# | Hello World, a2! |
# | Hello World, a3! |
# +------------------+
# 3 rows in set (0.001 sec)


g++ -fPIC -shared -o udf_hello_world_args.so udf_hello_world_args.cpp $(/home/mariadb/mariadb_11.4.3/bin/mysql_config --cflags --libs)