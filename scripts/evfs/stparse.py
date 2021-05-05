# Parser Script to pass through the fields within stuctures of 'struct_parser.h' as a dictionary with each field paired with its identifier and structure.
# The script also generates a header file named 'enum.h' containing enums of the fields parsed through the dictionary.
# The script also generates a source file named 'enum.c' that contains sizes of all enum fields parsed through the dictionary in arrays.
# The script also contains a function generator that uses functions that map the enums to their locations and values.
# Version 3.4

# List of Constants used as token checks.
STRUCT='struct'
UNION='union'
UNSIGNED='unsigned'
LONG='long'
DOUBLE='double'
INT='int'
SHORT='short'
CHAR='char'
OPEN_PARENTHESES='{'
CLOSE_PARENTHESES='}'
SPACE=''
CONST='const'

IDENTIFIERS=[CONST,STRUCT,UNSIGNED,LONG,INT,SHORT,CHAR,DOUBLE] # Array of all Identifiers.


file=open("struct_parser.h","r")
elements=list()
parsedict=dict()
identifier_dict=dict()


for x in file:
   x=x.lstrip()
   x=x.rstrip()
   x=x.replace('﻿','')
   x=x.replace('\n','')
   x=x.replace('}','')
   x=x.replace('union {','')
   if ';' in x:
      pos=x.find(';')
      x=x[:pos]
   if '/*' in x:
      length=len(x)
      pos1=x.find('/*')
      pos2=x.rfind('*/')
      x=x[:pos1]+x[pos2+2:]   
   elements.append(x)
   
# Creation of Dictionary containing fields of the structures.
values=[]
for fields in elements:
   if OPEN_PARENTHESES in fields and UNION not in fields:
      values=[]
      fields=fields.replace(' {','')
      if STRUCT in fields:
         key=fields.replace('struct ','')
         
               
   else: 
      values.append(fields)
      parsedict.update({key:values})



# Formatting the Dictionary based on structure's fields.

for item in parsedict:
      IDENTIFIERS.append(item)
      data = parsedict.get(item)
      contents=data
      for spaces in contents:
         if spaces == SPACE:
               contents.remove(spaces)
         if spaces == ' ':
               contents.remove(' ')        
      if '' in contents:
         contents.remove('')
      if '\n' in contents:
         contents.remove('\n')      
      parsedict.update({item:contents})

# Seperating Identifiers from fields - Creating a Dictionary of fields and identifiers within the structures Dictionary.
data=[]
internal_fields=[]
internal_fields_types=[]
for item in parsedict:
      data=parsedict.get(item)   
      for fields in data:
         type_ident=str()
         seperator=fields.split()
         for checks in IDENTIFIERS:
            if checks in seperator:
               type_ident+=' '+checks
               seperator.remove(checks)
         key=seperator[0]
         internal_fields.append(key)
         internal_fields_types.append(type_ident.lstrip())
         identifier_dict.update({key:type_ident.lstrip()})
         parsedict.update({item:identifier_dict})

      identifier_dict={}

#Splitting internal structures of structure's fields and flattening its fields from the Dictionary.
internal_fields_types=[]
parsedict_length=len(parsedict)
struct_length=[]
struct_list=[]
struct_index=0
for structs in parsedict:
   struct_list.append(structs)
   struct_length.append(len(parsedict.get(structs)))

split_fields=[]
split_fields_types=[]

def internal_splitting(struct_name:str):
   """The function creates a list of all fields present in the structure type so as to split the used structure's members into each of those fields.
   For example: struct evfs_timeval atime field of evfs_inode structure is split into its timeval types of atime_tv_sec and atime_tv_usec by appending the type's members to atime from the split_fields list """
   global split_fields
   global split_fields_types
   split_f=[]
   split_ft=[]
   split_struct= parsedict.get(struct_name)
   for fields in split_struct:
      split_f.append(fields)
      split_ft.append(split_struct.get(fields))
   split_fields=split_f
   split_fields_types=split_ft
      
#Restructuring and updating Dictionary post splitting of internal fields

for blocks in parsedict:
   pairs=parsedict.get(blocks)
   fields=pairs.keys()
   fields_type=pairs.values()
   fields_type=list(fields_type)
   fields=list(fields)
   for i in range(len(fields)):
      if 'struct' in fields_type[i]: #Checking if splitting of fields is required in internal structs
         check=fields_type[i]
         check=check.replace(STRUCT,'')
         check=check.replace(spaces,'')
         check=check.replace(CONST,'')
         check=check.lstrip()
         if check in struct_list:
            internal_splitting(check)
            for j in range(len(split_fields)):
               field_replace=fields[i]
               field1=fields[i]
               field1=field1+'_'+split_fields[j]
               parsedict[blocks].update({field1:split_fields_types[j]})
         else:
            raise("STRUCT ERROR: ",fields[i]," Structure was not parsed in struct_parser.h")
            
         del parsedict[blocks][field_replace]
      

# Final Dictionary Database.    
print(parsedict)

# Enum generator to create file 'enum.h'
PREFIX=''
enums_list=[]
fields_list=[]
enum_gen=open("enum.h","w+")
for blocks in parsedict:
   if blocks == 'evfs_inode':
      PREFIX = blocks[5:]
   else:
      PREFIX = blocks[5:]
   PREFIX = PREFIX.upper()+'_'
   enum_gen.write("enum " + blocks + "{\n")
   enum_gen.write(PREFIX + "INVALID_FIELD,\n\n/*auto-generated enums*/\n")
   pairs=parsedict.get(blocks)
   fields=pairs.keys()
   fields_type=pairs.values()
   fields_type=list(fields_type)
   fields=list(fields)
   for i in range(len(fields)):
      if fields[i]==fields[-1]:  
         fields[i]=fields[i].upper()
         enums_list.append(PREFIX+fields[i])
         enum_gen.write(PREFIX + fields[i] + "\n" + "/*end of auto-generated enums*/\n" + "};\n\n")
         fields_list.append(fields[i])
         internal_fields_types.append(fields_type[i])
      else:      
         fields[i]=fields[i].upper()
         enums_list.append(PREFIX+fields[i])
         enum_gen.write(PREFIX + fields[i] + ",\n")
         fields_list.append(fields[i])
         internal_fields_types.append(fields_type[i])
   
enum_gen.close()

# Enum Sizes Array Generator to create file 'enum.c'

array_name=''
enum_size=open("enum.c","w+")
enum_size.write("#include <stdio.h>\n#include <stdlib.h>\n#include <enum.h>\n#include <struct_parser.h>\n\nvoid main(){\n\n/* auto-generated enum size arrays */\n\n")

for blocks in parsedict:
   array_name=blocks[5:]
   block_fields=list(parsedict.get(blocks).keys())
   block_fields_types=list(parsedict.get(blocks).values())
   size_array="int "+array_name+"_sizes[]={\n"
   enum_size.write(size_array+'')
   for field_count in range(len(block_fields)):
      if field_count == len(block_fields)-1:
         enum_size.write("sizeof("+block_fields_types[field_count]+")\n};\n\n")
      else:
         enum_size.write("sizeof("+block_fields_types[field_count]+"),\n")

enum_size.close()      

# Implementation of enum mapping function generator for file 'enum.c'
function_parameters = ["case ","break","*fieldptr ","*size ","default","int","_enum_to_field"] #All standard syntactical terms for the functions which are repetitive
enum_func=open("enum.c","a")

for blocks in parsedict:
   func_name=blocks[5:]
   enum_func.write("int "+func_name+function_parameters[6]+"(int ev, struct "+blocks+" *"+func_name+", void **fieldptr, unsigned *size) {\n    switch(ev) {\n    ")
   for fields in list(parsedict.get(blocks).keys()):
      enum_func.write(function_parameters[0]+func_name.upper()+"_"+fields.upper()+":\n        ")
      enum_func.write(function_parameters[2]+"= &"+func_name+"->"+fields.upper()+";\n        ")
      enum_func.write(function_parameters[3]+"= "+func_name+"_sizes ["+func_name.upper()+"_"+fields.upper()+"];\n        "+function_parameters[1]+";\n    ")
   enum_func.write(function_parameters[4]+":\n        "+"return -EINVAL;\n    }\n    return 0;\n    }\n\n")

enum_func.write("return 0;\n}")
enum_func.close()

# List of all Enums, Fields and Field types.
print(enums_list)
print(fields_list)
print(internal_fields_types)

#End of File.
