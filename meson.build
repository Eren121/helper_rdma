project('infiniband', 'cpp',
    version : '1.0.0',
    license : 'MIT')

helper_tcp_dep = dependency('helper_tcp', fallback : ['helper_tcp', 'helper_tcp_dep'])
ibv_dep = dependency('libibverbs', required : true)
rdma_dep = dependency('librdmacm', required : true)

inc = include_directories('include')
sources = [
    'src/main.cpp',
    'src/rdma_base.cpp',
    'src/rdma_server.cpp',
    'src/rdma_client.cpp',
]

executable('infiniband', sources,
    dependencies : [helper_tcp_dep, ibv_dep, rdma_dep],
    include_directories : inc)