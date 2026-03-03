#pragma once

extern "C" {
#include <postgres.h>
#include <catalog/pg_type_d.h>
}
#include <string>
#include <parquet/schema.h>


class MetadataField {
public:
	MetadataField(int id, const std::string &name, Oid type) : id(id), name(name), type(type) {}
	MetadataField(int id, const std::string &name, Oid type, const std::string &doc) : id(id), name(name), type(type), doc(doc) {}

	int id;
	std::string name;
	Oid type;
	std::string doc;
};

class MetadataColumns {
public:
	static inline const MetadataField DELETE_FILE_PATH{2147483546, "file_path", CSTRINGOID, "Path of a file in which a deleted row is stored"};
	static inline const MetadataField DELETE_FILE_POS{2147483545, "pos", INT8OID, "Ordinal position of a deleted row in the data file"};
};

class MetadataSchemaBuilder {
public:
	static std::shared_ptr<::parquet::schema::GroupNode> transformToParquetSchema(const std::vector<MetadataField> &mf) {
		parquet::schema::NodeVector fields;
		for (auto &meta : mf)
		{
			switch (meta.type)
			{
				case INT8OID: {
					fields.push_back(::parquet::schema::PrimitiveNode::Make(meta.name,
						::parquet::Repetition::REQUIRED, ::parquet::Type::INT64, ::parquet::ConvertedType::NONE, -1, -1, -1, meta.id));
					break;
				}
				case CSTRINGOID: {
					fields.push_back(::parquet::schema::PrimitiveNode::Make(meta.name,
						::parquet::Repetition::REQUIRED, ::parquet::Type::BYTE_ARRAY, ::parquet::ConvertedType::UTF8, -1, -1, -1, meta.id));
					break;
				}
				default:
					elog(ERROR, "Datalake foreign table column %s type Not support.",
						meta.name.c_str());
					break;
			}
		}
		return std::static_pointer_cast<::parquet::schema::GroupNode>(
			::parquet::schema::GroupNode::Make("schema", ::parquet::Repetition::OPTIONAL, fields));
	}
};


