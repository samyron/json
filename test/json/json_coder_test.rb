#!/usr/bin/env ruby
# frozen_string_literal: true

require_relative 'test_helper'

class JSONCoderTest < Test::Unit::TestCase
  def test_json_coder_with_proc
    coder = JSON::Coder.new do |object|
      "[Object object]"
    end
    assert_equal %(["[Object object]"]), coder.dump([Object.new])
  end

  def test_json_coder_with_proc_with_unsupported_value
    coder = JSON::Coder.new do |object|
      Object.new
    end
    assert_raise(JSON::GeneratorError) { coder.dump([Object.new]) }
  end

  def test_json_coder_options
    coder = JSON::Coder.new(array_nl: "\n") do |object|
      42
    end

    assert_equal "[\n42\n]", coder.dump([Object.new])
  end

  def test_json_coder_load
    coder = JSON::Coder.new
    assert_equal [1,2,3], coder.load("[1,2,3]")
  end

  def test_json_coder_load_options
    coder = JSON::Coder.new(symbolize_names: true)
    assert_equal({a: 1}, coder.load('{"a":1}'))
  end
end
