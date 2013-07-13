require File.join(File.dirname(__FILE__), "../ext/narray")
#NArray.debug = true

RSpec.configure do |config|
  config.treat_symbols_as_metadata_keys_with_true_values = true
  config.filter_run :focus
  config.run_all_when_everything_filtered = true
end
#context :focus=>true do ... end

types = [
  NArray::DFloat,
  NArray::SFloat,
  NArray::DComplex,
  NArray::SComplex,
  NArray::Int64,
  NArray::Int32,
  NArray::Int16,
  NArray::Int8,
  NArray::UInt64,
  NArray::UInt32,
  NArray::UInt16,
  NArray::UInt8,
]
#types = [NArray::DFloat]

types.each do |dtype|

  describe dtype  do
    it{expect(dtype).to be < NArray}
  end

  procs = [
    [proc{|tp| tp[1,2,3,5,7,11] },""],
    [proc{|tp| tp[1,2,3,5,7,11][true] },"[true]"],
    [proc{|tp| tp[1,2,3,5,7,11][0..-1] },"[0..-1]"]
  ]
  procs.each do |init,ref|

    describe dtype,"[1,2,3,5,7,11]"+ref do
      before(:all) do
        @a = init.call(dtype)
      end

      it{expect(@a).to be_kind_of dtype}
      it{expect(@a.size).to eq 6}
      it{expect(@a.ndim).to eq 1}
      it{expect(@a.shape).to eq [6]}
      it{expect(@a).not_to be_inplace}
      it{expect(@a).to     be_row_major}
      it{expect(@a).not_to be_column_major}
      it{expect(@a).to     be_host_order}
      it{expect(@a).not_to be_byte_swapped}
      it{expect(@a).to eq [1,2,3,5,7,11]}
      it{expect(@a.to_a).to eq [1,2,3,5,7,11]}
      it{expect(@a.to_a).to be_kind_of Array}

      it{expect(@a.eq([1,1,3,3,7,7])).to eq [1,0,1,0,1,0]}
      it{expect(@a[3..4]).to eq [5,7]}
      it{expect(@a['1::2']).to eq [2,5,11]}
      it{expect(@a[5]).to eq 11}
      it{expect(@a[-1]).to eq 11}
      it{expect(@a.sum).to eq 29}
      #it{expect(@a.min).to eq 1}
      #it{expect(@a.max).to eq 11}
      it{expect(@a.copy.fill(12)).to eq [12]*6}
      it{expect((@a + 1)).to eq [2,3,4,6,8,12]}
      it{expect((@a - 1)).to eq [0,1,2,4,6,10]}
      it{expect((@a * 3)).to eq [3,6,9,15,21,33]}
      it{expect((@a / 0.5)).to eq [2,4,6,10,14,22]}
      it{expect((-@a)).to eq [-1,-2,-3,-5,-7,-11]}
      it{expect((@a ** 2)).to eq [1,4,9,25,49,121]}
      it{expect(@a.swap_byte.swap_byte).to eq [1,2,3,5,7,11]}

      after(:all) do
        @a = nil
      end
    end
  end

  #describe dtype, ".seq(5)" do
  #  it do
  #    dtype.seq(5).should == [0,1,2,3,4]
  #  end
  #end

  procs2 = [
    [proc{|tp,src| tp[*src] },""],
    [proc{|tp,src| tp[*src][true,true] },"[true,true]"],
    [proc{|tp,src| tp[*src][0..-1,0..-1] },"[0..-1,0..-1]"]
  ]
  procs2.each do |init,ref|

    describe dtype,'[[1,2,3],[5,7,11]]'+ref do
      before(:all) do
        @src = [[1,2,3],[5,7,11]]
        @a = init.call(dtype,@src)
      end

      it{expect(@a).to be_kind_of dtype}
      it{expect(@a.size).to eq 6}
      it{expect(@a.ndim).to eq 2}
      it{expect(@a.shape).to eq [2,3]}
      it{expect(@a).not_to be_inplace}
      it{expect(@a).to     be_row_major}
      it{expect(@a).not_to be_column_major}
      it{expect(@a).to     be_host_order}
      it{expect(@a).not_to be_byte_swapped}
      it{expect(@a).to eq @src}
      it{expect(@a.to_a).to eq @src}
      it{expect(@a.to_a).to be_kind_of Array}

      it{expect(@a.eq([[1,1,3],[3,7,7]])).to eq [[1,0,1],[0,1,0]]}
      it{expect(@a[5]).to eq 11}
      it{expect(@a[-1]).to eq 11}
      it{expect(@a[1,0]).to eq @src[1][0]}
      it{expect(@a[1,1]).to eq @src[1][1]}
      it{expect(@a[1,2]).to eq @src[1][2]}
      it{expect(@a['1::2']).to eq [2,5,11]}
      it{expect(@a[3..4]).to eq [5,7]}
      it{expect(@a[0,1..2]).to eq [2,3]}
      it{expect(@a['1,0::2']).to eq [5,11]}
      it{expect(@a[0,:*]).to eq @src[0]}
      it{expect(@a[1,:*]).to eq @src[1]}
      it{expect(@a[:*,1]).to eq [@src[0][1],@src[1][1]]}
      it{expect(@a.sum).to eq 29}
      #it{expect(@a.min).to eq 1}
      #it{expect(@a.max).to eq 11}
      it{expect(@a.copy.fill(12)).to eq [[12]*3]*2}
      it{expect((@a + 1)).to eq [[2,3,4],[6,8,12]]}
      it{expect((@a - 1)).to eq [[0,1,2],[4,6,10]]}
      it{expect((@a * 3)).to eq [[3,6,9],[15,21,33]]}
      it{expect((@a / 0.5)).to eq [[2,4,6],[10,14,22]]}
      it{expect((-@a)).to eq [[-1,-2,-3],[-5,-7,-11]]}
      it{expect((@a ** 2)).to eq [[1,4,9],[25,49,121]]}
      it{expect(@a.swap_byte.swap_byte).to eq @src}

      after(:all) do
        @a = nil
      end
    end

  end

end
