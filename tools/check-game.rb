#!/usr/bin/env ruby
# frozen_string_literal: true

require "json"

class ExecutableCheck
  SELF_MAGICS = [[0x4f, 0x15, 0x3d, 0x1d], [0x54, 0x14, 0xf5, 0xee]].freeze
  SELF_IDENT_VARIANTS = [
    [[0x00, 0x01, 0x01, 0x12, 0x01, 0x01, 0x00, 0x00], 0x22],
    [[0x10, 0x01, 0x01, 0x12, 0x01, 0x01, 0x00, 0x10], 0x32]
  ].freeze

  attr_reader :errors, :warnings, :kind, :segment_count

  def initialize(path)
    @path = path
    @errors = []
    @warnings = []
    @kind = "unknown"
    @segment_count = 0
  end

  def run
    File.open(@path, "rb") do |file|
      prefix = read_at(file, 0, 4)
      if prefix == "\x7fELF".b
        @kind = "ELF64"
        validate_elf(file, 0)
      elsif SELF_MAGICS.include?(prefix.bytes)
        @kind = "SELF"
        validate_self(file)
      else
        @errors << "unsupported executable magic #{prefix.unpack1('H*')}"
      end
    end
    self
  rescue Errno::ENOENT, Errno::EACCES => error
    @errors << error.message
    self
  rescue EOFError => error
    @errors << error.message
    self
  end

  def valid?
    @errors.empty?
  end

  private

  def read_at(file, offset, length)
    file.seek(offset)
    value = file.read(length)
    raise EOFError, "truncated file at 0x#{offset.to_s(16)}" unless value && value.bytesize == length

    value
  end

  def validate_self(file)
    header = read_at(file, 0, 32)
    ident, _size1, _size2, self_file_size, count, unknown, _pad =
      header.unpack("a12vvQ<vvV")
    @segment_count = count

    unless SELF_IDENT_VARIANTS.include?([ident.bytes[4, 8], unknown])
      @errors << format("unsupported SELF identity tail/variant (unknown=0x%04x)", unknown)
    end
    @warnings << "SELF file_size field exceeds container size" if self_file_size > file.size

    segments = count.times.map do |index|
      type, segment_offset, compressed_size, decompressed_size =
        read_at(file, 32 + (index * 32), 32).unpack("Q<Q<Q<Q<")
      {
        index: index,
        type: type,
        offset: segment_offset,
        compressed_size: compressed_size,
        decompressed_size: decompressed_size
      }
    end

    elf_offset = 32 + (count * 32)
    program_headers = validate_elf(file, elf_offset)
    return unless program_headers

    segments.each do |segment|
      next if (segment[:type] & 0x800).zero?

      program_index = (segment[:type] >> 20) & 0xfff
      program = program_headers[program_index]
      if program.nil?
        @errors << "SELF segment #{segment[:index]} references missing ELF program header #{program_index}"
        next
      end
      if segment[:compressed_size] != segment[:decompressed_size]
        @errors << "SELF segment #{segment[:index]} is compressed; current Magnus loader cannot expand it"
      end
      if segment[:decompressed_size] != program[:file_size]
        @errors << "SELF segment #{segment[:index]} size does not match ELF program header #{program_index}"
      end
      if segment[:offset] + segment[:compressed_size] > file.size
        @errors << "SELF segment #{segment[:index]} extends past end of file"
      end
    end
  end

  def validate_elf(file, offset)
    header = read_at(file, offset, 64)
    ident, type, machine, version, _entry, program_offset, _section_offset, _flags,
      header_size, program_entry_size, program_count, section_entry_size, _section_count,
      _section_name_index = header.unpack("a16vvVQ<Q<Q<Vvvvvvv")

    @errors << "embedded executable is not ELF" unless ident.bytes[0, 4] == [0x7f, 0x45, 0x4c, 0x46]
    @errors << "ELF is not 64-bit" unless ident.getbyte(4) == 2
    @errors << "ELF is not little-endian" unless ident.getbyte(5) == 1
    @errors << "ELF ident version is not current" unless ident.getbyte(6) == 1
    @errors << "ELF OS ABI is not FreeBSD/Prospero" unless ident.getbyte(7) == 9
    @errors << "unsupported ELF ABI version #{ident.getbyte(8)}" unless [0, 2].include?(ident.getbyte(8))
    @errors << format("unsupported ELF type 0x%04x", type) unless [0xfe10, 0xfe18].include?(type)
    @errors << "ELF machine is not x86-64" unless machine == 62
    @errors << "ELF version is not current" unless version == 1
    @errors << "unexpected ELF header size #{header_size}" unless header_size == 64
    @errors << "unexpected ELF program-header size #{program_entry_size}" unless program_entry_size == 56
    if section_entry_size.positive? && section_entry_size != 64
      @errors << "unexpected ELF section-header size #{section_entry_size}"
    end
    return nil unless program_entry_size == 56

    program_count.times.map do |index|
      values = read_at(file, offset + program_offset + (index * program_entry_size), 56)
               .unpack("VVQ<Q<Q<Q<Q<Q<")
      {
        type: values[0],
        flags: values[1],
        offset: values[2],
        virtual_address: values[3],
        file_size: values[5],
        memory_size: values[6]
      }
    end
  end
end

def fail_with(message)
  warn "ERROR: #{message}"
  exit 2
end

game_path = ARGV[0]
fail_with("usage: ruby tools/check-game.rb /path/to/PPSAxxxxx-app") unless game_path
game_path = File.expand_path(game_path)
fail_with("not a directory: #{game_path}") unless File.directory?(game_path)

puts "MagnusPS5 game-folder check"
puts "Path: #{game_path}"

overall_errors = []
overall_warnings = []

param_path = File.join(game_path, "sce_sys", "param.json")
if File.file?(param_path)
  begin
    params = JSON.parse(File.binread(param_path))
    title_id = params["titleId"]
    localized = params["localizedParameters"].is_a?(Hash) ? params["localizedParameters"] : {}
    language = localized["defaultLanguage"]
    localized_title = language && localized[language].is_a?(Hash) ? localized[language]["titleName"] : nil
    localized_title ||= localized.dig("en-US", "titleName")
    puts "Title: #{localized_title || '(not supplied)'}"
    puts "Title ID: #{title_id || '(not supplied)'}"
    puts "Content version: #{params['contentVersion'] || params['appVersion'] || '(not supplied)'}"
    puts "Required system software: #{params['requiredSystemSoftwareVersion'] || '(not supplied)'}"
    puts "Flexible guest memory: #{params.dig('kernel', 'flexibleMemorySize') || 1_073_741_824} bytes"
    overall_errors << "param.json has no titleId" unless title_id.is_a?(String) && !title_id.empty?

    folder_id = File.basename(game_path).sub(/-app\z/i, "")
    if title_id && folder_id.match?(/\APPSA\d+\z/i) && folder_id.casecmp(title_id) != 0
      overall_warnings << "folder title ID #{folder_id} differs from param.json title ID #{title_id}"
    end
  rescue JSON::ParserError => error
    overall_errors << "invalid sce_sys/param.json: #{error.message}"
  rescue Errno::EACCES => error
    overall_errors << error.message
  end
else
  overall_errors << "missing sce_sys/param.json"
end

eboot_path = File.join(game_path, "eboot.bin")
if File.file?(eboot_path)
  eboot = ExecutableCheck.new(eboot_path).run
  puts "eboot.bin: #{eboot.kind}, #{File.size(eboot_path)} bytes, #{eboot.segment_count} SELF segments"
  overall_errors.concat(eboot.errors.map { |message| "eboot.bin: #{message}" })
  overall_warnings.concat(eboot.warnings.map { |message| "eboot.bin: #{message}" })
else
  overall_errors << "missing eboot.bin"
end

module_paths = [game_path, File.join(game_path, "sce_module"), File.join(game_path, "sce_modules")]
               .select { |path| File.directory?(path) }
               .flat_map do |path|
  Dir.children(path).map do |name|
    candidate = File.join(path, name)
    candidate if File.file?(candidate) && name.match?(/\.(?:prx|sprx)\z/i) && name.casecmp("eboot.bin") != 0
  end.compact
end

module_failures = 0
module_paths.sort.each do |path|
  check = ExecutableCheck.new(path).run
  next if check.valid?

  module_failures += 1
  check.errors.each { |message| overall_errors << "#{path.delete_prefix(game_path + '/')}: #{message}" }
end
puts "Adjacent modules checked: #{module_paths.length} (#{module_failures} invalid)"

overall_warnings.each { |message| puts "WARNING: #{message}" }
overall_errors.each { |message| puts "ERROR: #{message}" }

if overall_errors.empty?
  puts "RESULT: STRUCTURALLY COMPATIBLE with the Magnus loader (playability is not guaranteed)"
  exit 0
end

puts "RESULT: NOT COMPATIBLE as currently laid out"
exit 1
