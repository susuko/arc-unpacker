require 'lib/formats/gfx/mgd_converter'
require 'lib/formats/script/msd_converter'

# FJSYS archive
# Company: various
# Extension: .fjsys
# Known games:
# - Sono Hanabira ni Kuchizuke o 1
# - Sono Hanabira ni Kuchizuke o 2
# - Sono Hanabira ni Kuchizuke o 3
# - Sono Hanabira ni Kuchizuke o 4
# - Sono Hanabira ni Kuchizuke o 5
# - Sono Hanabira ni Kuchizuke o 6
# - Sono Hanabira ni Kuchizuke o 7
# - Sono Hanabira ni Kuchizuke o 8
# - Sono Hanabira ni Kuchizuke o 9
# - Sono Hanabira ni Kuchizuke o 10
# - Sono Hanabira ni Kuchizuke o 11
module FjsysArchive
  MAGIC = "FJSYS\x00\x00\x00"

  def self.add_cli_help(arg_parser)
    arg_parser.add_help(
      '-k, --key=KEY',
      'Sets key used for decrypting MSD files.',
      possible_values: MsdConverter::COMMON_KEYS.keys)
  end

  def self.parse_cli_options(arg_parser, options)
    key = arg_parser.switch(%w(-k --key))
    key = MsdConverter::COMMON_KEYS[key.to_sym] unless key.nil?
    options[:msd_key] = key
  end

  class Unpacker
    def unpack(arc_file, output_files, options)
      magic = arc_file.read(MAGIC.length)
      fail ArcError, 'Not a FJSYS archive' unless magic == MAGIC

      header_size,
      file_names_size,
      file_count = arc_file.read(76).unpack('LLLx64')
      file_names_start = header_size - file_names_size

      file_count.times do
        output_files.write do
          file_name_origin,
          data_size,
          data_origin = arc_file.read(16).unpack('LLQ')

          file_name = arc_file.peek(file_name_origin + file_names_start) do
            arc_file.read_until_zero
          end

          data = arc_file.peek(data_origin) do
            decode(file_name, arc_file.read(data_size), options)
          end

          [file_name, data]
        end
      end
    end

    private

    def decode(file_name, data, options)
      if data.start_with?(MgdConverter::MAGIC)
        data = MgdConverter.decode(data)
      elsif file_name.downcase.end_with?('.msd')
        data = MsdConverter.decode(data, options[:msd_key])
      end

      data
    end
  end

  class Packer
    def pack(arc_file, input_files, _options)
      arc_file.write(MAGIC)

      file_names_start = input_files.length * 16 + 0x54
      file_names_size = input_files.names.map { |f| f.length + 1 }.reduce(0, :+)

      header_size = file_names_size + file_names_start

      arc_file.write([
        header_size,
        file_names_size,
        input_files.length].pack('LLLx64'))
      arc_file.write("\x00" * (header_size - arc_file.tell))

      table_entries = []
      input_files.each do |file_name, data|
        data = encode(file_name, data)

        table_entries.push(
          file_name: file_name,
          data_size: data.length,
          data_origin: arc_file.tell)

        arc_file.write(data)
      end

      table_entries = fix_file_order(table_entries)

      arc_file.seek(file_names_start)
      table_entries.each do |e|
        e[:file_name_origin] = arc_file.tell - file_names_start
        arc_file.write(e[:file_name])
        arc_file.write("\x00")
      end

      arc_file.seek(0x54)
      table_entries.each do |e|
        arc_file.write([
          e[:file_name_origin],
          e[:data_size],
          e[:data_origin]].pack('LLQ'))
      end
    end

    private

    def encode(file_name, data)
      if file_name.downcase.end_with?('.mgd')
        return MgdConverter.encode(data)
      elsif file_name.downcase.end_with?('.msd')
        return MsdConverter.encode(data)
      end
      data
    end

    # it is important to sort the files like the game did,
    # because the game refers to the file by their indices, not the file names!
    def fix_file_order(table_entries)
      # I guess the files are sorted alphabetically, but this assumption might
      # be wrong. I know that what I do below is stupid, but "_" needed to be
      # placed before "0" and after "." in the games I tested.
      table_entries.sort_by { |e| e[:file_name].gsub('_', '/').downcase }
    end
  end
end
