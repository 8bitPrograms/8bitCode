return CopySqlLanguage({
	"mssql",
}, {
	[0] = "select insert update delete merge from where and or not null is in like between join inner left right full outer cross apply outer on using group by order having union all distinct as create alter drop table view index trigger procedure proc function begin end if else while return declare set commit rollback grant revoke exists case cast true false default constraint primary key foreign references check add column with output pivot unpivot top percent identity identity_insert try catch throw raiserror exec execute go tran transaction nocount ansi_nulls quoted_identifier",
	[1] = "isnull coalesce len charindex substring left right ltrim rtrim replace getdate getutcdate sysdatetime count sum avg min max convert newid dateadd datediff datename datepart isnumeric isdate",
	[4] = "nvarchar nchar varchar char int bigint smallint tinyint bit decimal numeric money smallmoney float real datetime datetime2 date time datetimeoffset uniqueidentifier xml sql_variant hierarchyid image text ntext",
}, {
	["lexer.sql.allow.dotted.word"] = "1",
	["lexer.sql.numbersign.comment"] = "0",
	["fold.sql.at.else"] = "1",
})
