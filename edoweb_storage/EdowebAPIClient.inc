<?php
/**
 * Copyright 2013 hbz NRW (http://www.hbz-nrw.de/)
 *
 * This file is part of regal-drupal.
 *
 * regal-drupal is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * regal-drupal is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with regal-drupal.  If not, see <http://www.gnu.org/licenses/>.
 */

class EdowebAPIClient implements EdowebAPIClientInterface {

  /*
   * The URL of the Edoweb API
   */
  private $__edoweb_api_host;

  /**
   * The username for the Edoweb API
   *
   */
  private $__edoweb_api_user;

  /**
   * The password for the Edoweb API
   *
   */
  private $__edoweb_api_pass;

  public function __construct() {
    // Fetch credentials from drupal settings
    $this->__edoweb_api_host = variable_get('edoweb_api_host');
    $this->__edoweb_api_user = variable_get('edoweb_api_user');
    $this->__edoweb_api_pass = variable_get('edoweb_api_password');
  }

  /*
   * Load an entity from the API.
   *
   * @param $entity
   *   The drupal entity to load data into
   */
  public function load($entity) {
    $linkedDataClient = new LinkedDataClient();
    return $linkedDataClient->load($entity);
  }

  public function getTransformers($entity) {
    $http_url = sprintf(
      'http://%s:%s@%s/resource/%s.regal',
      $this->__edoweb_api_user,
      $this->__edoweb_api_pass,
      $this->__edoweb_api_host,
      $entity->remote_id
    );

    $http_response = $this->_http_get($http_url, 'application/json');
    if (200 != $http_response->code) {
      watchdog('edoweb_storage', 'Unable to load resource @id from @api.',
        array(
          '@id' => $entity->remote_id,
          '@api' => variable_get('edoweb_api_host'),
        )
      );
      return FALSE;
    }
    return json_decode($http_response->data)->transformer;
  }

  public function addURN($entity) {
    list($namespace, $id) = explode(':', $entity->remote_id);
    $snid = 'hbz:929:02';
    $http_url = sprintf(
      'http://%s:%s@%s/utils/addUrn?namespace=%s&id=%s&snid=%s',
      $this->__edoweb_api_user,
      $this->__edoweb_api_pass,
      $this->__edoweb_api_host,
      $namespace,
      $id,
      $snid
    );

    $http_response = $this->_http_post($http_url);
    if (200 != $http_response->code) {
      watchdog('edoweb_storage', 'Unable to generate URN for @id on @api.',
        array(
          '@id' => $entity->remote_id,
          '@api' => variable_get('edoweb_api_host'),
        )
      );
      return FALSE;
    }
    return TRUE;
  }

  /*
   * Save an entity to the API
   *
   * @param $entity
   *   The drupal entity to store data from
   *
   */
  public function save($entity) {

    // Local entities are stored in the file system
    if ('local:' == substr($entity->remote_id, 0, 6)) {
      $rdf_serializer = new LibRDF_Serializer('rdfxml');
      $metadata = _edoweb_storage_entity_serialize_rdf(
        $entity, $rdf_serializer, array('descriptive')
      );
      $file_name = substr($entity->remote_id, 6);
      $file_path = drupal_realpath(file_default_scheme() . '://');
      file_put_contents("$file_path/$file_name.rdf", $metadata);
    } else {
      $this->saveResource($entity);
      // Store attached files first, as entity metadata has to be
      // updated with corresponding new file URIs
      $this->saveFiles($entity);
      $this->saveMetadata($entity);
    }
  }

  public function saveMetadata($entity) {
    // Send metadata
    $http_post_data = _edoweb_storage_entity_serialize_ntriples(
      $entity, array('descriptive')
    );
    $http_url = sprintf(
      'http://%s:%s@%s/resource/%s/metadata',
      $this->__edoweb_api_user,
      $this->__edoweb_api_pass,
      $this->__edoweb_api_host,
      $entity->remote_id
    );
    $http_response = $this->_http_post($http_url, $http_post_data);
    if (200 != $http_response->code) {
      watchdog('edoweb_storage', 'Unable to save metadata for @id to @api.',
        array(
          '@id' => $entity->remote_id,
          '@api' => variable_get('edoweb_api_host'),
        )
      );
      return FALSE;
    }
    return TRUE;
  }

  public function saveFiles($entity) {
    $http_url = sprintf(
      'http://%s:%s@%s/resource/%s/data',
      $this->__edoweb_api_user,
      $this->__edoweb_api_pass,
      $this->__edoweb_api_host,
      $entity->remote_id
    );
    if (property_exists($entity, 'field_edoweb_datastream')
        && isset($entity->field_edoweb_datastream[LANGUAGE_NONE][0]['value'])) {
      $file_url = $entity->field_edoweb_datastream[LANGUAGE_NONE][0]['value'];
    }
    if (isset($file_url) && 'public://' == substr($file_url, 0, 9)) {
      $file_name = $entity->field_edoweb_label[LANGUAGE_NONE][0]['value'];
      $file_mime = $entity->field_edoweb_filetype[LANGUAGE_NONE][0]['value'];
      $file_path = drupal_realpath($file_url);
      if ($this->_http_multipart_put("$http_url", $file_path, $file_mime, $file_name)) {
        $entity->field_edoweb_datastream[LANGUAGE_NONE][0]['value'] =
          "http://{$this->__edoweb_api_host}/resource/{$entity->remote_id}/data";
        unlink($file_path);
        cache_clear_all("field:edoweb_basic:{$entity->local_id}", 'cache_field');
      } else {
        watchdog('edoweb_storage', 'Unable to upload file to API');
        return FALSE;
      }
    }
    return TRUE;
  }

  public function saveResource($entity, $transformers = null) {
    // Create resource
    $http_url = sprintf(
      'http://%s:%s@%s/resource/%s',
      $this->__edoweb_api_user,
      $this->__edoweb_api_pass,
      $this->__edoweb_api_host,
      $entity->remote_id
    );

    $data = array();
    $data['type'] = $entity->bundle_type;
    if ($transformers) $data['transformer'] = $transformers;

    $parents = field_get_items('edoweb_basic', $entity, 'field_edoweb_struct_parent');
    if (FALSE !== $parents) {
      $data['parentPid'] = $parents[0]['value'];
    }

    $http_response = $this->_http_put($http_url, json_encode($data), 'application/json');
    if (200 != $http_response->code) {
      watchdog('edoweb_storage', 'Unable to save @id to @api.',
        array(
          '@id' => $entity->remote_id,
          '@api' => variable_get('edoweb_api_host'),
        )
      );
      return FALSE;
    }
    return TRUE;
  }

  /*
   * Delete an entity via the API
   *
   * @param $entity
   *   The drupal entity to delete
   *
   */
  public function delete($entity) {
    // Delete resource
    $http_url = sprintf(
      'http://%s:%s@%s/resource/%s',
      $this->__edoweb_api_user,
      $this->__edoweb_api_pass,
      $this->__edoweb_api_host,
      $entity->remote_id
    );

    $http_response = $this->_http_delete($http_url);
    if (200 != $http_response->code) {
      watchdog('edoweb_storage', 'Unable to delete @id from @api.',
        array(
          '@id' => $entity->remote_id,
          '@api' => variable_get('edoweb_api_host'),
        )
      );
      return;
    }
  }

  /*
   * Import an entity based on its remote id.
   */
  public function importEntity($remote_id, $import_mode = 'overwrite') {

    if (is_null(LinkedDataClient::getTripleStore())) {
      $linkedDataClient = new LinkedDataClient();
      $http_url = _edoweb_expand_curie($remote_id);
      $repository_data = $linkedDataClient->getRDF($http_url);
    } else {
      $repository_data = LinkedDataClient::getTripleStore();
    }

    // Import the entity itself
    LibRDF_LiteralNode::setPlainOutput(true);
    $bundle_type = _edoweb_bundle_type_from_rdf(
      $remote_id, $repository_data
    );
    $entity = entity_get_controller('edoweb_basic')->create(
      array(
        'bundle_type' => $bundle_type,
        'uid' => 0,
      )
    );
    if ('overwrite' == $import_mode) {
      $local_id = db_select('edoweb_basic')
        ->condition('remote_id', $remote_id, '=')
        ->fields('edoweb_basic', array('local_id'))
        ->execute()
        ->fetchField();
      $entity->local_id = $local_id;
    }
    $entity->created = time();
    $entity->remote_id = $remote_id;
    $primary_keys = $entity->local_id ? 'local_id' : array();

    // Directly write out the entity record bypassing the controller to
    // prevent needlessly writing objects back to the API
    try {
      drupal_write_record('edoweb_basic', $entity, $primary_keys);
      watchdog('edoweb_storage', 'Imported @id.',
        array(
          '@id' => $remote_id,
        )
      );
    } catch (PDOException $e) {
      watchdog('edoweb_storage', 'Failed to import @id.',
        array(
          '@id' => $remote_id,
        )
      );
      return FALSE;
    }

    return TRUE;

  }

  /*
   * Import entities of a given bundle type.
   */
  public function import(
    $bundle_type,
    $import_mode = 'overwrite',
    $source = 'repo',
    $from = 0,
    $until = 10
  ){
    // Build resource list url
    $http_url = sprintf(
      'http://%s:%s@%s/resource?type=%s&getListingFrom=%s&from=%s&until=%s',
      $this->__edoweb_api_user,
      $this->__edoweb_api_pass,
      $this->__edoweb_api_host,
      $bundle_type,
      $source,
      $from,
      $until
    );

    // Fetch resource list
    $http_response = $this->_http_get($http_url, 'application/json');
    if ((200 != $http_response->code) || !isset($http_response->data)) {
      watchdog('edoweb_storage', 'Unable to fetch resource listing from @api.',
        array(
          '@api' => variable_get('edoweb_api_host'),
        )
      );
      return;
    }

    // Wipe existing records if requested
    if (('clear' == $import_mode) && (0 == $from)) {
      // Delete entity records
      $num_deleted = db_delete('edoweb_basic')
        ->condition('bundle_type', $bundle_type)
        ->execute();
      drupal_set_message("Deleted $num_deleted resources of type $bundle_type.");
    }

    // Process resource list
    $data = json_decode($http_response->data);
    $resource_list = property_exists($data, 'list') ? $data->list : array();
    foreach ($resource_list as $remote_id) {
      $this->importEntity($remote_id, $import_mode);
    }
    if ((count($resource_list) == 10)) {
      $this->import(
        $bundle_type, $import_mode, $source, $from + 10, $until + 10
      );
    }
  }

  public function index($entity) {
    // Index resource
    $http_url = sprintf(
      'http://%s:%s@%s/utils/index/%s?type=%s',
      $this->__edoweb_api_user,
      $this->__edoweb_api_pass,
      $this->__edoweb_api_host,
      $entity->remote_id,
      $entity->bundle_type
    );

    $http_response = $this->_http_post($http_url);
    if (200 != $http_response->code) {
      watchdog('edoweb_storage', 'Unable to index @id on @api.',
        array(
          '@id' => $entity->remote_id,
          '@api' => variable_get('edoweb_api_host'),
        )
      );
      return FALSE;
    }
    return TRUE;
  }

  public function query(EntityFieldQuery $efq) {
    static $query_count = 0;
    $page = pager_find_page($query_count);
    $efq->pager_num = $query_count;

    if (isset($efq->tags['elasticsearch'])) {
      $results = $this->_query_elasticsearch($efq, $page);
    } else if (isset($efq->tags['lobid'])) {
      $results = $this->_query_lobid($efq, $page);
    }

    $results_per_page = variable_get('edoweb_storage_results_per_page', 10);
    pager_default_initialize($results['hits'], $results_per_page, $query_count);
    $query_count++;
    return $results;
  }

  protected function _query_lobid(EntityFieldQuery $efq, $page = 0) {
    $lobid_host = variable_get('edoweb_lobid_host');
    $term = str_replace(
      ' ', '+', $efq->metaData['term']
    );
    $endpoint = $efq->metaData['endpoint'];
    $parameter = $efq->metaData['parameter'];

    $results_per_page = variable_get('edoweb_storage_results_per_page', 10);
    $from = array_key_exists('start', $efq->range)
      ? $efq->range['start']
      : $page * $results_per_page;
    $size = array_key_exists('length', $efq->range)
      ? $efq->range['length']
      : $results_per_page;

    $http_url = sprintf(
      'http://%s/%s?%s=%s&from=%s&size=%s',
      $lobid_host, $endpoint, $parameter, $term, $from, $size
    );
    $http_response = $this->_http_get(
      $http_url, 'text/plain'
    );
    if (200 != $http_response->code || !isset($http_response->data)) {
      watchdog('edoweb_storage', 'Unable to execute query on @api.',
        array(
          '@api' => $lobid_host,
        )
      );
      return FALSE;
    }
    $rdf_parser = new LibRDF_Parser('ntriples');
    $rdf_model = new LibRDF_Model(new LibRDF_Storage());
    $rdf_model->loadStatementsFromString(
      $rdf_parser,
      $http_response->data
    );
    $foaf_primary_topic = new LibRDF_URINode(
      'http://xmlns.com/foaf/0.1/primaryTopic'
    );
    $primary_topics = $rdf_model->findStatements(
      null, $foaf_primary_topic, null
    );
    $results = array(
      'hits' => 0,
      EDOWEB_ENTITY_TYPE => array(),
    );
    foreach ($primary_topics as $primary_topic) {
      $remote_id = substr($primary_topic->getObject(), 1, -1);
      $entity = entity_get_controller('edoweb_basic')->createFromRdf(
        $remote_id, $rdf_model
      );
      $results[EDOWEB_ENTITY_TYPE][$remote_id] = $entity;
      $results['hits']++;
    }
    return $results;
  }

  protected function _query_elasticsearch(EntityFieldQuery $efq, $page = 0) {
    // Search resources
    $http_url = sprintf(
      'http://%s:%s@%s:9200/%s/_search',
      $this->__edoweb_api_user,
      $this->__edoweb_api_pass,
      $this->__edoweb_api_host,
      variable_get('edoweb_api_namespace')
    );
    $query = $this->_efq_to_es($efq, $page);

    $http_response = $this->_http_post(
      $http_url, $query, 'application/json'
    );
    if (200 != $http_response->code || !isset($http_response->data)) {
      watchdog('edoweb_storage', 'Unable to execute query on @api.',
        array(
          '@api' => variable_get('edoweb_api_host'),
        )
      );
      return FALSE;
    }
    $hits = json_decode($http_response->data)->hits;
    $results = array(
      'hits' => $hits->total,
      EDOWEB_ENTITY_TYPE => array(),
    );
    $rdf_parser = new LibRDF_Parser('ntriples');
    foreach ($hits->hits as $hit) {
      // Parse results
      $rdf_model = new LibRDF_Model(new LibRDF_Storage());
      $options = array(
        'format' => 'application/nquads',
      );
      $rdf_ntriples = _edoweb_escape_utf8(jsonld_to_rdf($hit->_source, $options));
      $rdf_model = new LibRDF_Model(new LibRDF_Storage());
      $rdf_model->loadStatementsFromString(
        $rdf_parser,
        $rdf_ntriples
      );
      // Identify primary resource
      $ore_describes = $rdf_model->findStatements(
        null,
        new LibRDF_URINode(
          "http://www.openarchives.org/ore/terms/describes"
        ),
        null
      )->current();
      // Create entity
      if ($ore_describes) {
        $remote_id = substr($ore_describes->getObject(), 1, -1);
        $entity = entity_get_controller('edoweb_basic')->createFromRdf(
          $remote_id, $rdf_model
        );
        $results[EDOWEB_ENTITY_TYPE][$remote_id] = $entity;
      }
    }
    return $results;
  }

  protected function _efq_to_es(EntityFieldQuery $efq, $page = 0) {
    $query = array();
    $results_per_page = variable_get('edoweb_storage_results_per_page', 10);
    $query['from'] = array_key_exists('start', $efq->range)
      ? $efq->range['start']
      : $page * $results_per_page;
    $query['size'] = array_key_exists('length', $efq->range)
      ? $efq->range['length']
      : $results_per_page;

    if (array_key_exists('term', $efq->metaData)) {
      $query['query']['bool']['must'][]['query_string']['query']
        = $efq->metaData['term'];
    }

    if (array_key_exists('bundle', $efq->entityConditions)) {
      $target_bundles = $efq->entityConditions['bundle']['value'];
      if (!is_array($target_bundles)) {
        $target_bundles = array($target_bundles);
      }
      $query['query']['bool']['must'][]['field']["@graph.http://hbz-nrw.de/regal#contentType.@value"]
        = implode(' OR ', $target_bundles);
    }

    foreach($efq->propertyConditions as $property_condition) {
      // TODO: Handle via RDF mapping
      switch ($property_condition['column']) {
        case 'uid':
          $uids = $property_condition['value'];
          if (!is_array($uids)) {
            $uids = array($uids);
          }
          $query['query']['bool']['must'][]['field']
            ["@graph.http://purl.org/dc/elements/1.1/creator.@value"]
              = implode(' OR ', $uids);
          break;
      }
    }

    $rdf_mapping = rdf_mapping_load(EDOWEB_ENTITY_TYPE);
    $rdf_namespaces = rdf_get_namespaces();
    foreach($efq->fieldConditions as $field_condition) {
      $field_name = $field_condition['field']['field_name'];
      $field_value = $field_condition['value'];
      // Special cases for unmapped structural fields
      // TODO: remove superfluous special cases
      switch ($field_name) {
        case 'field_edoweb_struct_parent':
          $field_property = 'http://purl.org/dc/terms/isPartOf';
          $value_type = 'id';
          break;
        case 'field_edoweb_struct_child':
          $field_property = 'http://purl.org/dc/terms/hasPart';
          $value_type = 'id';
        default:
          // Map field name to full property URI
          // TODO: use OR query and all predicates
          $curie = $rdf_mapping[$field_name]['predicates'][0];
          list($prefix, $local) = explode(':', $curie);
          $field_property = $rdf_namespaces[$prefix] . $local;
          $value_type =
            (isset($rdf_mapping['type']) && $rdf_mapping['type'] == 'rel') ? 'id' : 'value';
          break;
      }
      $query['query']['bool']['must'][]['field']["@graph.$field_property.@$value_type"] = "\"$field_value\"";
    }

    if (!isset($query['query'])) {
      $query['query'] = array("match_all" => new stdClass());
    }

    $query['sort'] = array();
    foreach($efq->order as $sort) {
      $field_property = null;
      $value_type = null;
      switch ($sort['type']) {
        case 'property':
          $property_name = $sort['specifier'];
          if ($property_name == 'updated') {
            $field_property = _edoweb_expand_curie('dc:modified');
            $value_type = 'value';
          }
          break;
        case 'field':
          // FIXME: retrieve property from actual mapping
          // (problem is, we don't have a specific bundle type here)
          $field_name = $sort['specifier']['field']['field_name'];
          $field_info = field_info_field($field_name);
          $field_property = _edoweb_expand_curie(
            $field_info['settings']['predicates'][0]
          );
          $value_type = 'value';
          break;
        default:
          break;
      }
      $order = $sort['direction'];
      if ($field_property && $value_type) {
        $query['sort'][]["@graph.$field_property.@$value_type"] = array(
          'order' => $order,
          'ignore_unmapped' => TRUE,
        );
      }
    }

    return json_encode($query);
  }

  protected function _http_multipart_put($request_url, $file_path, $content_type = null, $file_name = null) {
    $data = array(
      'data' => "@$file_path",
      'type' => $content_type,
      'label' => $file_name,
    );
    $ch = curl_init($request_url);
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
    curl_setopt($ch, CURLOPT_CUSTOMREQUEST, 'PUT');
    curl_setopt($ch, CURLOPT_POSTFIELDS, $data);
    curl_setopt($ch, CURLOPT_HTTPHEADER, array('Content-type: multipart/mixed'));
    $response = curl_exec($ch);
    if (curl_errno($ch) && 1 == variable_get('edoweb_api_debug')) {
      drupal_set_message(curl_error($ch));
      return FALSE;
    } else {
      return TRUE;
    }
  }

  protected function _http_put($request_url, $request_body = '', $content_type='text/plain') {
    $http_options = array(
      'method' => 'PUT',
      'data' => $request_body,
      'headers' => array('Content-Type' => $content_type),
    );
    return _edoweb_http_request($request_url, $http_options);
  }

  protected function _http_post($request_url, $request_body = '', $content_type='text/plain') {
    $http_options = array(
      'method' => 'POST',
      'data' => $request_body,
      'headers' => array('Content-Type' => $content_type),
    );
    return _edoweb_http_request($request_url, $http_options);
  }

  protected function _http_get($request_url, $content_type = 'text/plain') {
    $http_options = array(
      'method' => 'GET',
      'headers' => array('Accept' => $content_type),
    );
    return _edoweb_http_request($request_url, $http_options);
  }

  protected function _http_delete($request_url) {
    $http_options = array(
      'method' => 'DELETE',
    );
    return _edoweb_http_request($request_url, $http_options);
  }

  protected function _http_head($request_url) {
    $http_options = array(
      'method' => 'HEAD',
    );
    return _edoweb_http_request($request_url, $http_options);
  }

}

