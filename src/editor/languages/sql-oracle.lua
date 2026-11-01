return CopySqlLanguage({
	"pls",
	"plb",
	"pkb",
	"pks",
	"pck",
	"tps",
	"tpb",
	"orasql",
}, {
	[0] = "select insert update delete merge from where and or not null is in like between join inner left right full outer cross on using group by order having union all distinct as create alter drop table view index sequence trigger procedure function package body declare begin end if then else elsif loop while for return into values set commit rollback savepoint grant revoke exception when others raise rownum rowid level start connect prior dual minus intersect fetch offset rows only with unique exists case cast true false default constraint primary key foreign references check add column replace type pragma autonomous_transaction",
	[1] = "sysdate systimestamp user uid nvl nvl2 decode to_char to_date to_number substr instr length trim ltrim rtrim replace greatest least count sum avg min max dbms_output dbms_lob",
	[3] = "set prompt define spool whenever rem column show execute",
	[4] = "varchar2 nvarchar2 number date timestamp clob blob nclob raw long integer pls_integer binary_integer boolean sys_refcursor bfile xmltype",
}, {
	["lexer.sql.allow.dotted.word"] = "1",
	["fold.sql.at.else"] = "1",
})
