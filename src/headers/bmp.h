#include <vector>
#include <fstream>

void put_integer(std::vector<char>& buffer, const uint v);
void put_short(std::vector<char>& buffer, const uint v);
void write_BMP(std::vector<uint32_t>& metadata, std::vector<std::vector<short>>& mcus, int start_dpu_index, const std::string& filename, std::vector<char>& write_buffer);

static inline void put_u32(std::vector<char>& buf, std::size_t& ofs, uint32_t v);
static inline void put_u16(std::vector<char>& buf, std::size_t& ofs, uint16_t v);
void write_BMP_fast(const std::vector<uint32_t>& meta, const std::vector<std::vector<short>>& mcus, int start_dpu, const std::string& filename);