//
// Created by Artur Troian on 10/20/16.
//
#include <propertypp/sqlite.hpp>
#include <tools/base64.hpp>

#include <exception>
#include <stdexcept>

#include <cstdlib>
#include <iostream>

namespace property {

int sqlite_property::select_exec_cb(void *ptr, int argc, char **argv, char **names)
{
	// suppress: warning: unused parameter 'names' [-Wunused-parameter]
	(void)names;

	req_value *value = reinterpret_cast<req_value *>(ptr);

	value->found = true;

	if (argc == 2) {
		value->blob  = std::string(argv[0]);
		value->type  = (value_type)atoi(argv[1]);
		value->valid = true;
	} else {
		value->valid = false;
	}

	return 0;
}

int sqlite_property::type_exec_cb(void *ptr, int argc, char **argv, char **names)
{
	// suppress: warning: unused parameter 'names' [-Wunused-parameter]
	(void)names;

	req_value *value = reinterpret_cast<req_value *>(ptr);

	value->found = true;

	if (argc == 1) {
		value->type  = (value_type)atoi(argv[0]);
		value->valid = true;
	} else {
		value->valid = false;
	}

	return 0;
}

const std::string sqlite_property::property_table_("property_table");

sqlite_property::sqlite_property(const std::string &db) :
	  prop()
	, db_(NULL)
{
	std::string uri(db);

	if (sqlite3_open(db.c_str(), &db_) != SQLITE_OK) {
		std::string error ("Couldn't open database file");
		error += sqlite3_errmsg(db_);
		sqlite3_close(db_);
		throw std::runtime_error(error);
	}

//	sqlite3_extended_result_codes(db_, true);
	const char *sql = "CREATE TABLE IF NOT EXISTS property_table(key STRING PRIMARY KEY, value BLOB, type INTEGER);";

	if (sqlite3_exec(db_, sql, 0, 0, 0) != SQLITE_OK) {
		std::string error ("Couldn't create table");
		error += sqlite3_errmsg(db_);
		sqlite3_close(db_);
		throw std::runtime_error(error);
	}
}

sqlite_property::~sqlite_property()
{
	sqlite3_close(db_);
}

prop_status sqlite_property::get(const std::string &key, void *value, value_type type)
{
	std::string sql = "SELECT value, type FROM " + property_table_ + " WHERE key = \'" + key + "\'";
	int ret;
	char *errmsg = NULL;
	req_value rsp;

	ret = sqlite3_exec(db_, sql.c_str(), select_exec_cb, &rsp, &errmsg);

	if (ret == SQLITE_OK) {
		if (rsp.found) {
			if (type != rsp.type)
				return prop_status::PROP_STATUS_INVALID_TYPE;
			else {
				switch (type) {
				case value_type::VALUE_TYPE_STRING: {
					std::string *val = reinterpret_cast<std::string *>(value);
					*val = rsp.blob;
					break;
				}
				case value_type::VALUE_TYPE_INT: {
					int32_t *val = reinterpret_cast<int32_t *>(value);
					*val = std::stoi(rsp.blob);
					break;
				}
				case value_type::VALUE_TYPE_INT64: {
					int64_t *val = reinterpret_cast<int64_t *>(value);
					*val = std::stoll(rsp.blob);
					break;
				}
				case value_type::VALUE_TYPE_DOUBLE: {
					double *val = reinterpret_cast<double *>(value);
					*val = std::stod(rsp.blob);
					break;
				}
				case value_type::VALUE_TYPE_BOOL: {
					bool *val = reinterpret_cast<bool *>(value);

					if (rsp.blob.compare("false") == 0)
						*val = false;
					else
						*val = true;

					break;
				}
				case value_type::VALUE_TYPE_BLOB: {
					prop::blob_type *val = reinterpret_cast<prop::blob_type *>(value);

					prop::blob_type tmp = tools::base64::decode<prop::blob_type>(rsp.blob);
					*val = std::move(tmp);
					break;
				}
				}
			}
		} else {
			return prop_status::PROP_STATUS_NOT_FOUND;
		}

	} else {
		sqlite3_free(errmsg);
		std::cout << "Error: " << ret << std::endl;
		return prop_status::PROP_STATUS_UNKNOWN_ERROR;
	}

	return prop_status::PROP_STATUS_OK;
}

prop_status sqlite_property::set(const std::string &key, const void * const val, value_type type, bool update)
{
	prop_status ret = prop_status::PROP_STATUS_OK;

	std::string sql = "INSERT INTO " + property_table_ + "(key, value, type) values (?,?,?)";
	sqlite3_stmt *stmt;

	std::string value;

	switch (type) {
	case value_type::VALUE_TYPE_STRING: {
		value = *(reinterpret_cast<const std::string * const>(val));
		break;
	}
	case value_type::VALUE_TYPE_INT: {
		const int32_t * const v = reinterpret_cast<const int32_t * const>(val);
		value = std::to_string(*v);
		break;
	}
	case value_type::VALUE_TYPE_INT64: {
		const int64_t * const v = reinterpret_cast<const int64_t * const>(val);
		value = std::to_string(*v);
		break;
	}
	case value_type::VALUE_TYPE_DOUBLE: {
		const double * const v = reinterpret_cast<const double * const>(val);
		value = std::to_string(*v);
		break;
	}
	case value_type::VALUE_TYPE_BOOL: {
		const bool * const v = reinterpret_cast<const bool * const>(val);
		value = (*v) == true ? "true" : "false";
		break;
	}
	case value_type::VALUE_TYPE_BLOB: {
		const prop::blob_type * const v = reinterpret_cast<const prop::blob_type * const>(val);
		tools::base64::encode(value, v);
		break;
	}
	default:
		break;
	}

	int rc = sqlite3_prepare_v2(db_, sql.c_str(), sql.size(), &stmt, NULL);

	if (rc == SQLITE_OK) {
		// bind values

		sqlite3_bind_text(stmt, 1, key.c_str(), key.size(), 0);
		sqlite3_bind_blob(stmt, 2, value.c_str(), value.size(), NULL);
		sqlite3_bind_int(stmt, 3, (int)type);

		// commit
		sqlite3_step(stmt);
		rc = sqlite3_finalize(stmt);

		if (rc != SQLITE_OK) {
			if (rc == SQLITE_CONSTRAINT) {
				if (update == false) {
					ret = prop_status::PROP_STATUS_ALREADY_EXISTS;
				} else {
					value_type prop_type;

					ret = sqlite_property::type(key, prop_type);
					if (ret == prop_status::PROP_STATUS_OK) {
						if (prop_type != type) {
							ret = prop_status::PROP_STATUS_INVALID_TYPE;
						} else {
							sql = "UPDATE " + property_table_ + " SET value = ? WHERE key = \'" + key + "\'";
							rc = sqlite3_prepare_v2(db_, sql.c_str(), sql.size(), &stmt, NULL);
							if (rc == SQLITE_OK) {
								sqlite3_bind_blob(stmt, 1, value.c_str(), value.size(), NULL);

								sqlite3_step(stmt);
								rc = sqlite3_finalize(stmt);

								if (rc != SQLITE_OK) {
									ret = prop_status::PROP_STATUS_UNKNOWN_ERROR;
									std::cerr << "Error commiting: " << sqlite3_errmsg(db_) << std::endl;
								} else {
									ret = prop_status::PROP_STATUS_OK;
								}
							} else {
								std::cerr << "Error commiting: " << sqlite3_errmsg(db_) << std::endl;
								ret = prop_status::PROP_STATUS_UNKNOWN_ERROR;
							}
						}
					} else {
						std::cerr << "Error commiting: " << sqlite3_errmsg(db_) << std::endl;
					}
				}
			} else {
				ret = prop_status::PROP_STATUS_UNKNOWN_ERROR;
			}
		}
	} else {
		std::cout << "Error commiting: " << sqlite3_errmsg(db_) << std::endl;
	}

	return ret;
}

prop_status sqlite_property::del(const std::string &key)
{
	prop_status ret = prop_status::PROP_STATUS_OK;

	std::string sql = "DELETE FROM " + property_table_ + " WHERE key = \'" + key + "\'";

	sqlite3_stmt *stmt;

	if (sqlite3_prepare_v2(db_, sql.c_str(), sql.size(), &stmt, NULL) == SQLITE_OK) {
		while (sqlite3_step(stmt) == SQLITE_DONE) {}
		ret = prop_status::PROP_STATUS_OK;
	} else {
		ret = prop_status::PROP_STATUS_UNKNOWN_ERROR;
	}

	sqlite3_finalize(stmt);

	return ret;
}

prop_status sqlite_property::type(const std::string &key, value_type &type) const
{
	prop_status retval = prop_status::PROP_STATUS_OK;

	std::string sql = "SELECT type FROM " + property_table_ + " WHERE key = \'" + key + "\'";

	int ret;
	char *errmsg = NULL;
	req_value rsp;

	ret = sqlite3_exec(db_, sql.c_str(), type_exec_cb, &rsp, &errmsg);

	if (ret == SQLITE_OK) {
		if (rsp.found) {
	   	    type = rsp.type;
		} else {
			retval = prop_status::PROP_STATUS_NOT_FOUND;
		}
	} else {
		sqlite3_free(errmsg);
		retval = prop_status::PROP_STATUS_UNKNOWN_ERROR;
	}

	return retval;
}

prop_status sqlite_property::type(const std::string &key, value_type &type)
{
	prop_status retval = prop_status::PROP_STATUS_OK;

	std::string sql = "SELECT type FROM " + property_table_ + " WHERE key = \'" + key + "\'";

	int ret;
	char *errmsg = NULL;
	req_value rsp;

	ret = sqlite3_exec(db_, sql.c_str(), type_exec_cb, &rsp, &errmsg);

	if (ret == SQLITE_OK) {
		if (rsp.found) {
			type = rsp.type;
		} else {
			retval = prop_status::PROP_STATUS_NOT_FOUND;
		}
	} else {
		sqlite3_free(errmsg);
		retval = prop_status::PROP_STATUS_UNKNOWN_ERROR;
	}

	return retval;
}

} // namespace property
